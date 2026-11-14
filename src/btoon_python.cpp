// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// CPython extension for BTOON.
//
// Encoding walks Python objects straight into btoon::Encoder, with the thread attached
// (default(), tzinfo.utcoffset() and numpy can run Python code), depth-limited so cycles
// fail cleanly. Compression and signing then run with the GIL released.
//
// Decoding parses with core's hardened decoder (GIL released), then converts core's value
// tree to Python objects, releasing each part of the tree once it is converted. All
// parsing of untrusted bytes happens in btoon-core.
//
// Free-threaded CPython: the module declares that it doesn't need the GIL. That holds
// because:
//  - module state (Cache) is written once, while the module is first initialized, and is
//    read-only afterwards;
//  - the encoder never keeps a borrowed reference into a container someone else can
//    change: lists are copied with PySequence_Tuple, and dicts are copied under a
//    critical section with PyDict_Next, before anything that could run Python code. Each
//    container is therefore written from one consistent snapshot;
//  - buffers (bytearray, memoryview, array.array, numpy) are read while their export is
//    held, so their memory can't be freed or resized underneath the encoder; a concurrent
//    write to the elements gives unspecified element values, never a crash;
//  - decode() reads either an immutable bytes object or a private copy of the input, and
//    builds objects no other thread can see until it returns.
#include <pybind11/pybind11.h>

#include <datetime.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "btoon/btoon.h"
#include "btoon/encoder.h"

namespace py = pybind11;
using namespace btoon;

namespace {

// --- Small CPython helpers ------------------------------------------------------------

#if defined(_MSC_VER)
#define BTOON_NOINLINE __declspec(noinline)
#else
#define BTOON_NOINLINE __attribute__((noinline))
#endif

[[noreturn]] BTOON_NOINLINE void throw_python_error() { throw py::error_already_set(); }

// A new reference, or the pending Python error (e.g. MemoryError) as a C++ exception.
inline py::object steal(PyObject* p) {
    if (!p) [[unlikely]] throw_python_error();
    return py::reinterpret_steal<py::object>(p);
}

py::object borrow(PyObject* p) { return py::reinterpret_borrow<py::object>(p); }

// Locks `o` for the scope on free-threaded builds (a no-op with a GIL). Unlike
// Py_BEGIN/END_CRITICAL_SECTION, it is released when an exception leaves the scope.
class CriticalSection {
public:
    explicit CriticalSection(PyObject* o) {
#ifdef Py_GIL_DISABLED
        PyCriticalSection_Begin(&cs_, o);
#else
        (void)o;
#endif
    }
    ~CriticalSection() {
#ifdef Py_GIL_DISABLED
        PyCriticalSection_End(&cs_);
#endif
    }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;

private:
#ifdef Py_GIL_DISABLED
    PyCriticalSection cs_;
#endif
};

// A buffer export that is always released, also when the code using it throws.
class Buffer {
public:
    Buffer() = default;
    ~Buffer() {
        if (held_) PyBuffer_Release(&view_);
    }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // False (with the Python error set) if `o` doesn't export a buffer with these flags.
    bool acquire(PyObject* o, int flags) {
        held_ = PyObject_GetBuffer(o, &view_, flags) == 0;
        return held_;
    }
    const Py_buffer& view() const { return view_; }
    // The bytes in C order. Copies only if the buffer isn't C-contiguous.
    std::span<const uint8_t> contiguous(std::vector<uint8_t>& scratch) const {
        if (PyBuffer_IsContiguous(&view_, 'C')) {
            return {static_cast<const uint8_t*>(view_.buf), static_cast<size_t>(view_.len)};
        }
        scratch.resize(static_cast<size_t>(view_.len));
        // const_cast: Python 3.10's PyBuffer_ToContiguous takes a non-const view (it doesn't write to it).
        if (PyBuffer_ToContiguous(scratch.data(), const_cast<Py_buffer*>(&view_), view_.len, 'C') != 0) {
            throw py::error_already_set();
        }
        return {scratch.data(), scratch.size()};
    }

private:
    Py_buffer view_{};
    bool held_ = false;
};

// sys.modules[name] as a new reference, or NULL if it isn't there (no error set).
PyObject* loaded_module(PyObject* sys_modules, PyObject* name) {
#if PY_VERSION_HEX >= 0x030D0000
    PyObject* m = nullptr;
    if (PyDict_GetItemRef(sys_modules, name, &m) < 0) throw py::error_already_set();
    return m;
#else
    PyObject* m = PyDict_GetItemWithError(sys_modules, name);  // the GIL keeps it alive until the incref
    if (!m && PyErr_Occurred()) throw py::error_already_set();
    Py_XINCREF(m);
    return m;
#endif
}

// --- Module state ---------------------------------------------------------------------

// Objects cached at import time. Written once while the module is first initialized,
// read-only afterwards, and intentionally leaked: they must outlive every call, and must
// not be destroyed after the interpreter finalizes.
struct Cache {
    PyTypeObject *Timestamp, *Date, *DateTime, *ExtType, *array;
    PyObject *BtoonError, *BtoonTypeError;
    PyObject* utc;
    PyObject* sys_modules;
    PyObject *int_from_bytes, *int_to_bytes;  // int.from_bytes, int.to_bytes
    // Interned names (attribute lookups and calls without allocating).
    PyObject *seconds, *nanoseconds, *offset_minutes, *epoch_milliseconds, *epoch_nanoseconds;
    PyObject *code, *data, *utcoffset, *tolist, *bit_length, *big, *frombytes;
    PyObject *numpy, *numpy_ma, *ndarray, *MaskedArray, *signed_kwnames;
};
Cache* C = nullptr;

PyObject* intern(const char* s) {
    PyObject* o = PyUnicode_InternFromString(s);
    if (!o) throw py::error_already_set();
    return o;
}

PyObject* new_ref(py::handle h) { return h.inc_ref().ptr(); }

// --- Errors ---------------------------------------------------------------------------

[[noreturn]] void raise_btoon_obj(const char* code, PyObject* message, bool type_error) {
    PyObject* cls = type_error ? C->BtoonTypeError : C->BtoonError;
    PyObject* exc = PyObject_CallFunction(cls, "sO", code, message);
    if (exc) {
        PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exc)), exc);
        Py_DECREF(exc);
    }
    throw py::error_already_set();  // the BtoonError, or e.g. a MemoryError creating it
}

[[noreturn]] void raise_btoon(const char* code, const std::string& message, bool type_error = false) {
    py::object msg = steal(PyUnicode_DecodeUTF8(message.data(), static_cast<Py_ssize_t>(message.size()), "replace"));
    raise_btoon_obj(code, msg.ptr(), type_error);
}

[[noreturn]] void raise_from(const BtoonException& e) { raise_btoon(e.code_name(), e.what()); }

[[noreturn]] void raise_type(const std::string& what, PyObject* o) {
    raise_btoon("invalid_argument", what + ", not " + Py_TYPE(o)->tp_name, true);
}

// --- civil date helpers (proleptic Gregorian) --------------------------------------------

int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
}

// --- Python -> bytes (direct, no intermediate Value tree) ------------------------------

Timestamp datetime_to_timestamp(PyObject* o) {
    int64_t days = days_from_civil(PyDateTime_GET_YEAR(o), PyDateTime_GET_MONTH(o), PyDateTime_GET_DAY(o));
    int64_t secs = days * 86400 + PyDateTime_DATE_GET_HOUR(o) * 3600LL + PyDateTime_DATE_GET_MINUTE(o) * 60LL +
                   PyDateTime_DATE_GET_SECOND(o);
    uint32_t nanos = static_cast<uint32_t>(PyDateTime_DATE_GET_MICROSECOND(o)) * 1000u;
    // Aware datetimes are normalized to UTC; naive ones are taken to be UTC already.
    py::object off = steal(PyObject_CallMethodNoArgs(o, C->utcoffset));
    if (!off.is_none()) {
        PyObject* td = off.ptr();
        // A datetime subclass can override utcoffset() to return anything. Reading a
        // non-timedelta with the PyDateTime_DELTA_* macros is a heap over-read, and a huge
        // timedelta overflows the arithmetic below. CPython's own datetime enforces the
        // same rules.
        if (!PyDelta_Check(td)) raise_type("utcoffset() must return a timedelta or None", td);
        if (PyDateTime_DELTA_GET_DAYS(td) < -1 || PyDateTime_DELTA_GET_DAYS(td) > 0 ||
            (PyDateTime_DELTA_GET_DAYS(td) == -1 && PyDateTime_DELTA_GET_SECONDS(td) == 0 &&
             PyDateTime_DELTA_GET_MICROSECONDS(td) == 0)) {
            raise_btoon("invalid_argument", "utcoffset() must be strictly between -24 hours and 24 hours");
        }
        int64_t off_us = (PyDateTime_DELTA_GET_DAYS(td) * 86400LL + PyDateTime_DELTA_GET_SECONDS(td)) * 1000000LL +
                         PyDateTime_DELTA_GET_MICROSECONDS(td);
        int64_t total_us = secs * 1000000LL + nanos / 1000 - off_us;
        int64_t sec = total_us / 1000000;
        int64_t r = total_us % 1000000;
        if (r < 0) {
            r += 1000000;
            --sec;
        }
        secs = sec;
        nanos = static_cast<uint32_t>(r * 1000);
    }
    return Timestamp(secs, nanos);
}

std::string_view utf8_of(PyObject* str) {
    Py_ssize_t n = 0;
    const char* s = PyUnicode_AsUTF8AndSize(str, &n);  // cached on the str object; fails on lone surrogates
    if (!s) throw py::error_already_set();
    return {s, static_cast<size_t>(n)};
}

// An int field of a btoon value type, as int64. The dataclasses validate their fields, so
// this only fails for objects changed with object.__setattr__ or odd subclasses.
int64_t int64_of(PyObject* v, const char* what) {
    if (!PyLong_Check(v)) raise_type(std::string(what) + " must be an int", v);
    int overflow = 0;
    long long x = PyLong_AsLongLongAndOverflow(v, &overflow);
    if (x == -1 && PyErr_Occurred()) throw py::error_already_set();
    if (overflow) raise_btoon("invalid_argument", std::string(what) + " must fit in a signed 64-bit integer");
    return x;
}

int64_t int64_attr(PyObject* o, PyObject* name, const char* what) {
    py::object v = steal(PyObject_GetAttr(o, name));
    return int64_of(v.ptr(), what);
}

// The element type of a buffer format: 'f' (float32) or 'd' (float64), with an optional
// struct byte-order prefix. numpy reports a long double as 'g'; where it is 8 bytes it is
// a float64 (the caller checks the item size).
bool float_format(const char* fmt, char& code, bool& big_endian) {
    if (!fmt) return false;
    big_endian = std::endian::native == std::endian::big;
    if (*fmt == '<' || *fmt == '>' || *fmt == '!' || *fmt == '=' || *fmt == '@') {
        if (*fmt == '<') big_endian = false;
        if (*fmt == '>' || *fmt == '!') big_endian = true;
        ++fmt;
    }
    if ((fmt[0] != 'f' && fmt[0] != 'd' && fmt[0] != 'g') || fmt[1] != '\0') return false;
    code = fmt[0] == 'g' ? 'd' : fmt[0];
    return true;
}

template <class T>
void gather(const Py_buffer& v, bool swap, std::vector<T>& out) {
    const Py_ssize_t n = v.shape ? v.shape[0] : v.len / v.itemsize;
    const Py_ssize_t stride = v.strides ? v.strides[0] : v.itemsize;
    out.resize(static_cast<size_t>(n));
    const char* p = static_cast<const char*>(v.buf);
    for (Py_ssize_t i = 0; i < n; ++i) {
        uint8_t b[sizeof(T)];
        std::memcpy(b, p + i * stride, sizeof(T));
        if (swap) std::reverse(b, b + sizeof(T));
        std::memcpy(&out[static_cast<size_t>(i)], b, sizeof(T));
    }
}

// What encode() does with an object. One classification for both encoding and deciding
// whether default() must be asked first, so the two can't disagree.
enum class Kind : uint8_t {
    nil, true_, false_, integer, floating, str, bytes, ext, sequence, dict, datetime, buffer,
    date, date_time, timestamp, array, ndarray, unsupported,
};

class PyEncoder {
public:
    PyEncoder(Encoder& enc, size_t max_depth, bool auto_tabular, PyObject* default_fn)
        : enc_(enc), max_depth_(max_depth), tabular_(auto_tabular), default_(default_fn) {}

    void encode(PyObject* o, size_t depth) {
        // The commonest leaves first (exact types: bool isn't an exact int).
        if (PyLong_CheckExact(o)) return encode_int(o);
        if (PyUnicode_CheckExact(o)) return enc_.encodeString(utf8_of(o));
        if (PyFloat_CheckExact(o)) return enc_.encodeFloat(PyFloat_AS_DOUBLE(o));
        Kind k = classify(o);
        if (k != Kind::unsupported) return encode_as(o, k, depth);
        // default()'s replacement takes the object's place, at the same depth.
        py::object replacement = resolve_default(o, k);
        encode_as(replacement.ptr(), k, depth);
    }

private:
    Kind classify(PyObject* o) const {
        if (o == Py_None) return Kind::nil;
        if (o == Py_True) return Kind::true_;
        if (o == Py_False) return Kind::false_;
        if (PyLong_Check(o)) return Kind::integer;
        if (PyFloat_Check(o)) return Kind::floating;
        if (PyUnicode_Check(o)) return Kind::str;
        if (PyBytes_Check(o)) return Kind::bytes;
        if (PyTuple_Check(o)) {
            // ExtType is a namedtuple: recognize it before treating tuples as sequences.
            return !PyTuple_CheckExact(o) && PyObject_TypeCheck(o, C->ExtType) ? Kind::ext : Kind::sequence;
        }
        if (PyList_Check(o)) return Kind::sequence;
        if (PyDict_Check(o)) return Kind::dict;
        if (PyDateTime_Check(o)) return Kind::datetime;
        if (PyByteArray_Check(o) || PyMemoryView_Check(o)) return Kind::buffer;
        // Real type checks (not isinstance, which an object can fool with __class__).
        // Date and DateTime are Timestamp subclasses: check them first.
        if (PyObject_TypeCheck(o, C->Date)) return Kind::date;
        if (PyObject_TypeCheck(o, C->DateTime)) return Kind::date_time;
        if (PyObject_TypeCheck(o, C->Timestamp)) return Kind::timestamp;
        if (PyObject_TypeCheck(o, C->array)) return Kind::array;
        return is_ndarray(o) ? Kind::ndarray : Kind::unsupported;
    }

    // A numpy array, found without importing numpy (none can exist until it is imported).
    // Masked arrays are unsupported: as vectors they would leak the masked values.
    static bool is_ndarray(PyObject* o) {
        py::object np = py::reinterpret_steal<py::object>(loaded_module(C->sys_modules, C->numpy));
        if (!np) return false;
        PyObject* nd = PyObject_GetAttr(np.ptr(), C->ndarray);
        if (!nd) {  // e.g. sys.modules["numpy"] = None, the usual way to hide numpy
            PyErr_Clear();
            return false;
        }
        py::object guard = steal(nd);
        if (!PyType_Check(nd) || !PyObject_TypeCheck(o, reinterpret_cast<PyTypeObject*>(nd))) return false;
        return Py_TYPE(o) == reinterpret_cast<PyTypeObject*>(nd) || !is_masked_array(o);
    }

    static bool is_masked_array(PyObject* o) {
        py::object ma = py::reinterpret_steal<py::object>(loaded_module(C->sys_modules, C->numpy_ma));
        if (!ma) return false;
        PyObject* cls = PyObject_GetAttr(ma.ptr(), C->MaskedArray);
        if (!cls) {
            PyErr_Clear();
            return false;
        }
        py::object guard = steal(cls);
        return PyType_Check(cls) && PyObject_TypeCheck(o, reinterpret_cast<PyTypeObject*>(cls));
    }

    // Asks default() until it returns something BTOON can encode, which `kind` then
    // describes. A default() that keeps returning objects it must be asked about again
    // (its argument, say) fails with max_depth after max_depth + 1 calls in a row.
    py::object resolve_default(PyObject* o, Kind& kind) {
        if (!default_) {
            if (is_masked_array(o)) {
                raise_btoon("invalid_argument",
                            "numpy masked arrays are not BTOON serializable: convert them first (for example "
                            "with .filled() or .tolist()), or pass default=",
                            true);
            }
            raise_btoon("invalid_argument",
                        std::string("object of type '") + Py_TYPE(o)->tp_name + "' is not BTOON serializable", true);
        }
        py::object current = borrow(o);
        for (size_t calls = 0;; ++calls) {
            if (calls > max_depth_) {
                raise_btoon("max_depth", "default() kept returning objects BTOON can't encode (" +
                                             std::to_string(calls) + " calls in a row for one value)");
            }
            current = steal(PyObject_CallOneArg(default_, current.ptr()));
            kind = classify(current.ptr());
            if (kind != Kind::unsupported) return current;
        }
    }

    void encode_as(PyObject* o, Kind k, size_t depth) {
        switch (k) {
            case Kind::nil: return enc_.encodeNil();
            case Kind::true_: return enc_.encodeBool(true);
            case Kind::false_: return enc_.encodeBool(false);
            case Kind::integer: return encode_int(o);
            case Kind::floating: return enc_.encodeFloat(PyFloat_AS_DOUBLE(o));
            case Kind::str: return enc_.encodeString(utf8_of(o));
            case Kind::bytes:
                return enc_.encodeBinary({reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(o)),
                                          static_cast<size_t>(PyBytes_GET_SIZE(o))});
            case Kind::ext: return encode_ext(o);
            case Kind::sequence: return encode_sequence(o, depth);
            case Kind::dict: return encode_dict(o, depth);
            case Kind::datetime: return enc_.encodeTimestamp(datetime_to_timestamp(o));
            case Kind::buffer: return encode_buffer(o);
            case Kind::date: return enc_.encodeDate(int64_attr(o, C->epoch_milliseconds, "btoon.Date.epoch_milliseconds"));
            case Kind::date_time:
                return enc_.encodeDateTime(int64_attr(o, C->epoch_nanoseconds, "btoon.DateTime.epoch_nanoseconds"));
            case Kind::timestamp: return encode_timestamp(o);
            case Kind::array:
            case Kind::ndarray: {
                // 1-D float32/float64 buffers are float vectors; anything else goes through tolist().
                if (encode_float_vector(o)) return;
                py::object list = steal(PyObject_CallMethodNoArgs(o, C->tolist));
                const Kind lk = classify(list.ptr());
                // A subclass whose tolist() returns an array again would recurse without
                // ever reaching the depth limit.
                if (lk == Kind::array || lk == Kind::ndarray) raise_type("tolist() must return a list or a scalar", list.ptr());
                return encode(list.ptr(), depth);
            }
            case Kind::unsupported: break;
        }
        raise_btoon("invalid_argument", std::string("object of type '") + Py_TYPE(o)->tp_name + "' is not BTOON serializable",
                    true);
    }

    void check_depth(size_t depth) const {
        if (depth >= max_depth_) raise_btoon("max_depth", "maximum nesting depth exceeded (or the object contains a cycle)");
    }

    // A raw extension. The ids BTOON defines (-1…-6, -10) are refused (SPEC §5.7): they are
    // written from typed values, never verbatim. The code is checked here too, because
    // ExtType._make() and tuple.__new__() skip ExtType's own validation.
    void encode_ext(PyObject* o) {
        py::object code = steal(PyObject_GetAttr(o, C->code));
        int overflow = 0;
        long c = PyLong_Check(code.ptr()) ? PyLong_AsLongAndOverflow(code.ptr(), &overflow) : 0;
        if (c == -1 && PyErr_Occurred()) throw py::error_already_set();
        if (!PyLong_Check(code.ptr()) || PyBool_Check(code.ptr()) || overflow || c < -128 || c > 127) {
            raise_btoon("invalid_argument", "ExtType code must be an int in -128..127");
        }
        const char* typed = nullptr;
        switch (c) {
            case -1: typed = "timestamp extension: encode a datetime or a btoon.Timestamp"; break;
            case -2: typed = "date extension: encode a btoon.Date"; break;
            case -3: typed = "datetime extension: encode a btoon.DateTime"; break;
            case -4: typed = "bigint extension: encode an int"; break;
            case -5: typed = "float32 vector extension: encode an array.array('f') or a float32 numpy array"; break;
            case -6: typed = "float64 vector extension: encode an array.array('d') or a float64 numpy array"; break;
            case -10: typed = "tabular extension: encode a list of dicts"; break;
            default: break;
        }
        if (typed) {
            raise_btoon("invalid_argument", "ExtType(" + std::to_string(c) + ", ...) is BTOON's " + typed +
                                                " instead (spec §5.7)");
        }
        py::object data = steal(PyObject_GetAttr(o, C->data));
        if (!PyBytes_Check(data.ptr())) raise_type("ExtType data must be bytes", data.ptr());
        enc_.encodeExtension(static_cast<int8_t>(c), {reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(data.ptr())),
                                                      static_cast<size_t>(PyBytes_GET_SIZE(data.ptr()))});
    }

    void encode_int(PyObject* o) {
        int overflow = 0;
        long long v = PyLong_AsLongLongAndOverflow(o, &overflow);
        if (v == -1 && PyErr_Occurred()) throw py::error_already_set();
        if (!overflow) return enc_.encodeInt(v);
        if (overflow > 0) {
            unsigned long long u = PyLong_AsUnsignedLongLong(o);
            if (!(u == static_cast<unsigned long long>(-1) && PyErr_Occurred())) return enc_.encodeUint(u);
            PyErr_Clear();  // OverflowError: it needs a bigint
        }
        // Arbitrary precision: two's complement big-endian bytes (SPEC §5.4). int's own
        // methods, so an int subclass can't substitute other bytes.
        py::object bits = steal(PyObject_CallMethodOneArg(reinterpret_cast<PyObject*>(&PyLong_Type), C->bit_length, o));
        size_t n = PyLong_AsSize_t(bits.ptr());
        if (n == static_cast<size_t>(-1) && PyErr_Occurred()) throw py::error_already_set();
        py::object length = steal(PyLong_FromSize_t((n + 8) / 8));
        PyObject* args[] = {o, length.ptr(), C->big, Py_True};
        py::object b = steal(PyObject_Vectorcall(C->int_to_bytes, args, 3, C->signed_kwnames));
        if (!PyBytes_Check(b.ptr())) raise_type("int.to_bytes() returned", b.ptr());
        enc_.encodeBigInt({reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(b.ptr())),
                           static_cast<size_t>(PyBytes_GET_SIZE(b.ptr()))});
    }

    // bytearray / memoryview -> binary, as bytes(o) would give them (C order).
    void encode_buffer(PyObject* o) {
        Buffer buf;
        if (!buf.acquire(o, PyBUF_FULL_RO)) throw py::error_already_set();
        std::vector<uint8_t> scratch;
        enc_.encodeBinary(buf.contiguous(scratch));
    }

    // A 1-D float32/float64 buffer (array.array, numpy) -> float vector, whatever its
    // strides, alignment or byte order. False, with nothing written, for any other buffer.
    // The element type comes from the buffer itself, which an ndarray subclass can't
    // misreport the way it can override .dtype.
    bool encode_float_vector(PyObject* o) {
        Buffer buf;
        if (!buf.acquire(o, PyBUF_RECORDS_RO)) {  // e.g. object or datetime64 arrays: no buffer
            PyErr_Clear();
            return false;
        }
        const Py_buffer& v = buf.view();
        char code = 0;
        bool big_endian = false;
        if (v.ndim != 1 || v.suboffsets || !float_format(v.format, code, big_endian)) return false;
        const bool swap = big_endian != (std::endian::native == std::endian::big);
        if (code == 'f') {
            if (v.itemsize != sizeof(float)) return false;
            write_vector<float>(v, swap);
        } else {
            if (v.itemsize != sizeof(double)) return false;
            write_vector<double>(v, swap);
        }
        return true;
    }

    template <class T>
    void write_vector(const Py_buffer& v, bool swap) {
        const Py_ssize_t n = v.shape ? v.shape[0] : v.len / v.itemsize;
        const bool packed = !v.strides || v.strides[0] == static_cast<Py_ssize_t>(sizeof(T)) || n <= 1;
        if (!swap && packed && reinterpret_cast<uintptr_t>(v.buf) % alignof(T) == 0) {
            return put_vector(std::span<const T>(static_cast<const T*>(v.buf), static_cast<size_t>(n)));
        }
        std::vector<T> copy;  // strided, misaligned or byte-swapped: native, packed values
        gather(v, swap, copy);
        put_vector(std::span<const T>(copy));
    }

    void put_vector(std::span<const float> v) { enc_.encodeVectorFloat(v); }
    void put_vector(std::span<const double> v) { enc_.encodeVectorDouble(v); }

    void encode_timestamp(PyObject* o) {
        int64_t secs = int64_attr(o, C->seconds, "Timestamp.seconds");
        int64_t nanos = int64_attr(o, C->nanoseconds, "Timestamp.nanoseconds");
        if (nanos < 0 || nanos >= 1000000000) raise_btoon("invalid_argument", "Timestamp.nanoseconds must be in [0, 1e9)");
        py::object off = steal(PyObject_GetAttr(o, C->offset_minutes));
        if (off.is_none()) return enc_.encodeTimestamp(Timestamp(secs, static_cast<uint32_t>(nanos)));
        int64_t minutes = int64_of(off.ptr(), "Timestamp.offset_minutes");
        if (minutes < -1080 || minutes > 1080) raise_btoon("invalid_argument", "Timestamp.offset_minutes must be within ±1080");
        enc_.encodeTimestamp(Timestamp(secs, static_cast<uint32_t>(nanos), static_cast<int16_t>(minutes)));
    }

    struct Item {
        std::string_view key;  // the key's UTF-8, owned by key_obj
        py::object key_obj;
        py::object value;
    };

    // A dict's items with strong references, in the dict's order. The copy is taken under
    // the dict's critical section, so it is consistent even while another thread changes
    // the dict, and nothing in it runs Python code.
    static void dict_items(PyObject* dict, std::vector<Item>& out) {
        out.clear();
        CriticalSection cs(dict);
        out.reserve(static_cast<size_t>(PyDict_GET_SIZE(dict)));
        Py_ssize_t pos = 0;
        PyObject *k, *v;
        while (PyDict_Next(dict, &pos, &k, &v)) out.push_back({{}, borrow(k), borrow(v)});
    }

    // A dict's items, sorted by the UTF-8 of their keys (canonical order, SPEC §4.4). Keys
    // must be str, and distinct as UTF-8 (a str subclass can make equal-looking keys
    // distinct dict keys).
    void sorted_items(PyObject* dict, std::vector<Item>& out) {
        dict_items(dict, out);
        sort_items(out);
    }

    void sort_items(std::vector<Item>& out) {
        for (Item& it : out) {
            if (!PyUnicode_Check(it.key_obj.ptr())) raise_type("map keys must be str", it.key_obj.ptr());
            it.key = utf8_of(it.key_obj.ptr());
        }
        std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.key < b.key; });
        for (size_t i = 1; i < out.size(); ++i) {
            if (out[i].key == out[i - 1].key) {
                py::object msg = steal(PyUnicode_FromFormat(
                    "dict has two keys that are the same string, %R (a str subclass with its own __eq__ or "
                    "__hash__?): BTOON map keys must be unique",
                    out[i].key_obj.ptr()));
                raise_btoon_obj("invalid_argument", msg.ptr(), false);
            }
        }
    }

    void encode_dict(PyObject* o, size_t depth) {
        check_depth(depth);
        std::vector<Item> items;
        sorted_items(o, items);
        enc_.encodeMapHeader(items.size());
        for (const Item& it : items) {
            enc_.encodeString(it.key);
            encode(it.value.ptr(), depth + 1);
        }
    }

    void encode_sequence(PyObject* o, size_t depth) {
        check_depth(depth);
        // Snapshot lists (tuples are immutable): user code (default(), tzinfo, other threads)
        // may change them mid-encode. PySequence_Tuple copies a list atomically.
        py::object snapshot = PyTuple_CheckExact(o) ? borrow(o) : steal(PySequence_Tuple(o));
        PyObject* t = snapshot.ptr();
        const Py_ssize_t n = PyTuple_GET_SIZE(t);
        std::vector<py::object> replaced;  // default()'s answers, if a table check needed them
        if (tabular_ && n >= 2 && try_tabular(t, n, depth, replaced)) return;
        enc_.encodeArrayHeader(static_cast<size_t>(n));
        for (Py_ssize_t i = 0; i < n; ++i) {
            const size_t u = static_cast<size_t>(i);
            encode(u < replaced.size() && replaced[u] ? replaced[u].ptr() : PyTuple_GET_ITEM(t, i), depth + 1);
        }
    }

    // SPEC §5.6.2, the rule core applies to its own values: ≥ 2 elements, every one a map
    // (a dict, or an object default() turns into one), the first with ≥ 1 key, all with
    // the same keys (compared as UTF-8, like core), and the size rule.
    bool try_tabular(PyObject* t, Py_ssize_t n, size_t depth, std::vector<py::object>& replaced) {
        std::vector<PyObject*> rows(static_cast<size_t>(n));
        Py_ssize_t width = -1;
        for (Py_ssize_t r = 0; r < n; ++r) {
            PyObject* row = PyTuple_GET_ITEM(t, r);
            if (!PyDict_Check(row)) {
                // Only default() can say what an unsupported object is. Its answer replaces
                // the element for the plain path too, so it is asked once per element.
                if (!default_ || classify(row) != Kind::unsupported) return false;
                if (replaced.empty()) replaced.resize(static_cast<size_t>(n));
                Kind k;
                replaced[static_cast<size_t>(r)] = resolve_default(row, k);
                if (k != Kind::dict) return false;
                row = replaced[static_cast<size_t>(r)].ptr();
            }
            // A quick check on the live size; the snapshots below are what count.
            if (width < 0) width = PyDict_GET_SIZE(row);
            if (width == 0 || PyDict_GET_SIZE(row) != width) return false;
            rows[static_cast<size_t>(r)] = row;
        }

        // The first row in its own order, and the column (sorted) order of its keys.
        std::vector<Item> first;
        dict_items(rows[0], first);
        const size_t c = first.size();
        if (c == 0) return false;
        std::vector<Item> columns(first);
        sort_items(columns);
        std::vector<size_t> lengths;
        std::vector<std::string_view> names;
        lengths.reserve(c);
        names.reserve(c);
        for (const Item& it : columns) {
            lengths.push_back(it.key.size());
            names.push_back(it.key);
        }
        if (!btoon::tabular_beneficial(static_cast<size_t>(n), lengths)) return false;
        std::vector<size_t> column_of(c);  // first-row position -> column
        for (size_t col = 0; col < c; ++col) {
            for (size_t j = 0; j < c; ++j) {
                if (first[j].key_obj.ptr() == columns[col].key_obj.ptr()) column_of[j] = col;
            }
        }

        // Every row's values in column order, held strongly: cells run user code (default(),
        // utcoffset()) that may change or free the rows.
        std::vector<py::object> cells(static_cast<size_t>(n) * c);
        for (size_t j = 0; j < c; ++j) cells[column_of[j]] = first[j].value;
        std::vector<Item> items;
        for (size_t r = 1; r < rows.size(); ++r) {
            dict_items(rows[r], items);
            if (items.size() != c) return false;
            // Usually a row has the very same key objects as the first row, in the same order
            // (the same code built them): then the bytes match too, and no sort is needed.
            bool same_keys = true;
            for (size_t j = 0; j < c && same_keys; ++j) same_keys = items[j].key_obj.ptr() == first[j].key_obj.ptr();
            if (same_keys) {
                for (size_t j = 0; j < c; ++j) cells[r * c + column_of[j]] = std::move(items[j].value);
                continue;
            }
            sort_items(items);
            for (size_t col = 0; col < c; ++col) {
                if (items[col].key != columns[col].key) return false;
                cells[r * c + col] = std::move(items[col].value);
            }
        }
        if (depth + 1 >= max_depth_) raise_btoon("max_depth", "maximum nesting depth exceeded (or the object contains a cycle)");

        enc_.beginTabular(names, static_cast<size_t>(n));
        for (size_t col = 0; col < c; ++col) {
            enc_.beginTabularColumn();
            for (size_t r = 0; r < rows.size(); ++r) encode(cells[r * c + col].ptr(), depth + 2);
            enc_.endTabularColumn();
        }
        enc_.endTabular();
        return true;
    }

    Encoder& enc_;
    size_t max_depth_;
    bool tabular_;
    PyObject* default_;  // borrowed from the call's arguments; NULL for none
};

// --- Value -> Python -------------------------------------------------------------------

enum class TimestampMode { datetime, object };
enum class VectorMode { array, list, numpy };

struct DecodeCtx {
    TimestampMode timestamps;
    VectorMode vectors;
    bool strict;
    // Repeated keys (e.g. every row of a table) share one str object. The cache owns its
    // keys: the decoded tree is freed while it is converted.
    std::unordered_map<std::string, py::object> keys;
    std::unordered_map<int, py::object> zones;
};

// Whether a converted value still owns heap memory that resetting it would free now: its
// bytes, or an array's (by now empty) buffer. A string stored inline (the small-string
// optimization) owns none; a map frees its own nodes when it is converted.
bool owns_memory(const Value& v) {
    switch (v.index()) {
        case 5: {
            const std::string& s = *std::get_if<String>(&v);
            const char* self = reinterpret_cast<const char*>(&s);
            return !(s.data() >= self && s.data() < self + sizeof(s));
        }
        case 6: return std::get_if<Binary>(&v)->capacity() != 0;
        case 7: return std::get_if<Array>(&v)->capacity() != 0;
        case 9: return std::get_if<Extension>(&v)->data.capacity() != 0;
        case 13: return std::get_if<BigInt>(&v)->bytes.capacity() != 0;
        case 14: return std::get_if<VectorFloat>(&v)->data.capacity() != 0;
        case 15: return std::get_if<VectorDouble>(&v)->data.capacity() != 0;
        default: return false;
    }
}

// Core guarantees that map keys and column names are valid UTF-8, in non-strict mode too
// (SPEC §7): two invalid keys could otherwise become the same str and drop a value.
py::object key_object(const std::string& k, DecodeCtx& ctx) {
    auto decode_key = [&k] { return steal(PyUnicode_DecodeUTF8(k.data(), static_cast<Py_ssize_t>(k.size()), "strict")); };
    if (k.size() > 64) return decode_key();
    auto it = ctx.keys.find(k);
    if (it != ctx.keys.end()) return it->second;
    py::object obj = decode_key();
    if (ctx.keys.size() < 8192) ctx.keys.emplace(k, obj);
    return obj;
}

py::object make_timestamp(int64_t seconds, uint32_t nanos, std::optional<int16_t> offset) {
    py::object s = steal(PyLong_FromLongLong(seconds));
    py::object ns = steal(PyLong_FromUnsignedLong(nanos));
    py::object off = offset ? steal(PyLong_FromLong(*offset)) : py::none();
    return steal(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(C->Timestamp), s.ptr(), ns.ptr(), off.ptr(),
                                              nullptr));
}

py::object make_epoch(PyTypeObject* type, int64_t value) {
    py::object v = steal(PyLong_FromLongLong(value));
    return steal(PyObject_CallOneArg(reinterpret_cast<PyObject*>(type), v.ptr()));
}

// An aware datetime (microsecond precision; UTC, or the stored offset), or a null object if
// the local time is outside datetime's years 1–9999 (the caller falls back to a btoon type).
py::object aware_datetime(int64_t seconds, uint32_t nanos, std::optional<int16_t> offset, DecodeCtx& ctx) {
    // Years 1–9999 are about -6.2e10…2.5e11 s around 1970, and an offset moves local time by
    // at most 18 hours. Checking first keeps the arithmetic below far from int64 overflow:
    // seconds can be anything an int64 holds.
    constexpr int64_t kFirst = -62135596800LL - 86400, kLast = 253402300799LL + 86400;
    if (seconds < kFirst || seconds > kLast) return {};
    const int64_t local = seconds + (offset ? *offset * 60LL : 0);
    const int64_t days = local >= 0 ? local / 86400 : (local - 86399) / 86400;
    const int64_t sod = local - days * 86400;
    int64_t y;
    unsigned mo, d;
    civil_from_days(days, y, mo, d);
    if (y < 1 || y > 9999) return {};
    PyObject* tz = C->utc;
    if (offset) {
        auto it = ctx.zones.find(*offset);
        if (it == ctx.zones.end()) {
            py::object delta = steal(PyDelta_FromDSU(0, *offset * 60, 0));
            it = ctx.zones.emplace(*offset, steal(PyTimeZone_FromOffset(delta.ptr()))).first;
        }
        tz = it->second.ptr();
    }
    return steal(PyDateTimeAPI->DateTime_FromDateAndTime(
        static_cast<int>(y), static_cast<int>(mo), static_cast<int>(d), static_cast<int>(sod / 3600),
        static_cast<int>(sod / 60 % 60), static_cast<int>(sod % 60), static_cast<int>(nanos / 1000), tz,
        PyDateTimeAPI->DateTimeType));
}

// floor(value / unit) and the remainder in [0, unit), without overflow.
void split_epoch(int64_t value, int64_t unit, int64_t& seconds, uint32_t& nanos_units) {
    int64_t s = value / unit, r = value % unit;
    if (r < 0) {
        r += unit;
        --s;
    }
    seconds = s;
    nanos_units = static_cast<uint32_t>(r);
}

template <class T>
py::object vector_to_py(const std::vector<T>& data, const char* typecode, const char* dtype, DecodeCtx& ctx) {
    const Py_ssize_t n = static_cast<Py_ssize_t>(data.size());
    if (ctx.vectors == VectorMode::list) {
        py::object list = steal(PyList_New(n));
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyList_SET_ITEM(list.ptr(), i, steal(PyFloat_FromDouble(static_cast<double>(data[static_cast<size_t>(i)]))).release().ptr());
        }
        return list;
    }
    // A read-only view of core's buffer, copied once into the result (the view dies here).
    char* mem = const_cast<char*>(reinterpret_cast<const char*>(data.data()));
    py::object view = steal(PyMemoryView_FromMemory(mem, n * static_cast<Py_ssize_t>(sizeof(T)), PyBUF_READ));
    if (ctx.vectors == VectorMode::numpy) {
        py::object np = steal(PyImport_ImportModule("numpy"));
        py::object arr = steal(PyObject_CallMethod(np.ptr(), "frombuffer", "Os", view.ptr(), dtype));
        return steal(PyObject_CallMethod(arr.ptr(), "copy", nullptr));
    }
    py::object a = steal(PyObject_CallFunction(reinterpret_cast<PyObject*>(C->array), "s", typecode));
    steal(PyObject_CallMethodOneArg(a.ptr(), C->frombytes, view.ptr()));
    return a;
}

py::object to_py(Value& v, DecodeCtx& ctx);

// The containers and extensions are out of line, so the scalar path of to_py(), which runs
// once per value, stays small.
BTOON_NOINLINE py::object array_to_py(Array& a, DecodeCtx& ctx) {
    py::object list = steal(PyList_New(static_cast<Py_ssize_t>(a.size())));
    for (size_t i = 0; i < a.size(); ++i) {
        Value& e = a[i];
        PyList_SET_ITEM(list.ptr(), static_cast<Py_ssize_t>(i), to_py(e, ctx).release().ptr());
        // Free what it owned now, not with the rest of the tree. Scalars own nothing.
        if (e.index() >= 5 && owns_memory(e)) e = Value();
    }
    return list;
}

BTOON_NOINLINE py::object map_to_py(Map& m, DecodeCtx& ctx) {
    py::object d = steal(PyDict_New());
    // Each value is released once converted; the nodes go in one pass at the end (erasing
    // them one by one would rebalance the tree every time).
    for (auto& [k, e] : m) {
        py::object key = key_object(k, ctx);
        py::object val = to_py(e, ctx);
        if (PyDict_SetItem(d.ptr(), key.ptr(), val.ptr()) != 0) throw py::error_already_set();
        if (e.index() >= 5 && owns_memory(e)) e = Value();
    }
    m.clear();
    return d;
}

BTOON_NOINLINE py::object extension_to_py(const Value& v, DecodeCtx& ctx) {
    switch (v.index()) {
        case 9: {
            const auto& e = std::get<Extension>(v);
            py::object code = steal(PyLong_FromLong(e.type));
            py::object data = steal(PyBytes_FromStringAndSize(reinterpret_cast<const char*>(e.data.data()),
                                                              static_cast<Py_ssize_t>(e.data.size())));
            return steal(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(C->ExtType), code.ptr(), data.ptr(), nullptr));
        }
        case 10: {
            const auto& ts = std::get<Timestamp>(v);
            std::optional<int16_t> off;
            if (ts.has_timezone) off = ts.timezone_offset;
            if (ctx.timestamps == TimestampMode::datetime) {
                if (py::object dt = aware_datetime(ts.seconds, ts.nanoseconds, off, ctx)) return dt;
            }
            return make_timestamp(ts.seconds, ts.nanoseconds, off);
        }
        case 11: {
            const int64_t ms = std::get<Date>(v).milliseconds;
            if (ctx.timestamps == TimestampMode::datetime) {
                int64_t s;
                uint32_t r;
                split_epoch(ms, 1000, s, r);
                if (py::object dt = aware_datetime(s, r * 1000000u, std::nullopt, ctx)) return dt;
            }
            return make_epoch(C->Date, ms);  // also the fallback outside datetime's range
        }
        case 12: {
            const int64_t ns = std::get<DateTime>(v).nanoseconds;
            if (ctx.timestamps == TimestampMode::datetime) {
                int64_t s;
                uint32_t r;
                split_epoch(ns, 1000000000, s, r);
                if (py::object dt = aware_datetime(s, r, std::nullopt, ctx)) return dt;
            }
            return make_epoch(C->DateTime, ns);
        }
        case 13: {
            const auto& b = std::get<BigInt>(v).bytes;
            py::object raw = steal(PyBytes_FromStringAndSize(reinterpret_cast<const char*>(b.data()), static_cast<Py_ssize_t>(b.size())));
            PyObject* args[] = {raw.ptr(), C->big, Py_True};
            return steal(PyObject_Vectorcall(C->int_from_bytes, args, 2, C->signed_kwnames));
        }
        case 14: return vector_to_py(std::get<VectorFloat>(v).data, "f", "float32", ctx);
        case 15: return vector_to_py(std::get<VectorDouble>(v).data, "d", "float64", ctx);
    }
    return py::none();
}

// Converts `v`, freeing each part of core's tree once it is converted instead of all of it at
// the end. What the C allocator can hand back or reuse (large strings and blobs, chiefly)
// then no longer adds to the peak on top of the Python objects.
py::object to_py(Value& v, DecodeCtx& ctx) {
    switch (v.index()) {
        case 0: return py::none();
        case 1: return py::bool_(std::get<Bool>(v));
        case 2: return steal(PyLong_FromLongLong(std::get<Int>(v)));
        case 3: return steal(PyLong_FromUnsignedLongLong(std::get<Uint>(v)));
        case 4: return steal(PyFloat_FromDouble(std::get<Float>(v)));
        case 5: {
            const auto& s = std::get<String>(v);
            return steal(PyUnicode_DecodeUTF8(s.data(), static_cast<Py_ssize_t>(s.size()), ctx.strict ? "strict" : "surrogateescape"));
        }
        case 6: {
            const auto& b = std::get<Binary>(v);
            return steal(PyBytes_FromStringAndSize(reinterpret_cast<const char*>(b.data()), static_cast<Py_ssize_t>(b.size())));
        }
        case 7: return array_to_py(std::get<Array>(v), ctx);
        case 8: return map_to_py(std::get<Map>(v), ctx);
        default: return extension_to_py(v, ctx);
    }
}

// --- Options ------------------------------------------------------------------------------

// True if `o` is the str `s` (no allocation, no exception).
bool is_str(PyObject* o, const char* s) { return PyUnicode_Check(o) && PyUnicode_CompareWithASCIIString(o, s) == 0; }

CompressionAlgorithm parse_algorithm(PyObject* name) {
    if (!PyUnicode_Check(name)) raise_type("compression must be a str", name);
    if (is_str(name, "zlib")) return CompressionAlgorithm::ZLIB;
    if (is_str(name, "lz4")) return CompressionAlgorithm::LZ4;
    if (is_str(name, "zstd")) return CompressionAlgorithm::ZSTD;
    if (is_str(name, "brotli")) return CompressionAlgorithm::BROTLI;
    if (is_str(name, "snappy")) return CompressionAlgorithm::SNAPPY;
    if (is_str(name, "auto")) return CompressionAlgorithm::AUTO;
    py::object msg = steal(PyUnicode_FromFormat("unknown compression algorithm: %R", name));
    raise_btoon_obj("invalid_argument", msg.ptr(), true);
}

// The HMAC key (SPEC §8). None is "no key"; a key is non-empty bytes or str (as UTF-8).
std::optional<std::string> key_from(PyObject* key) {
    if (key == Py_None) return std::nullopt;
    std::string k;
    if (PyBytes_Check(key)) {
        k.assign(PyBytes_AS_STRING(key), static_cast<size_t>(PyBytes_GET_SIZE(key)));
    } else if (PyUnicode_Check(key)) {
        Py_ssize_t n = 0;
        const char* s = PyUnicode_AsUTF8AndSize(key, &n);
        if (!s) {
            if (!PyErr_ExceptionMatches(PyExc_UnicodeEncodeError)) throw py::error_already_set();
            PyErr_Clear();  // a lone surrogate has no UTF-8 form
            raise_btoon("invalid_argument", "hmac_key str contains a lone surrogate", true);
        }
        k.assign(s, static_cast<size_t>(n));
    } else {
        raise_btoon("invalid_argument", "hmac_key must be bytes or str", true);
    }
    // An empty key is a misconfiguration, not "no key" (SPEC §8): never silently skip verification.
    if (k.empty()) raise_btoon("invalid_argument", "hmac_key must not be empty (pass None for no key)", true);
    return k;
}

// SPEC §7: a deeper limit would let input (or a deep object) exhaust the native stack.
// btoon/__init__.py checks this first; this also covers direct calls into _btoon.
void check_max_depth(size_t max_depth) {
    if (max_depth > kMaxDepthLimit) {
        raise_btoon("invalid_argument", "max_depth must be at most " + std::to_string(kMaxDepthLimit), true);
    }
}

py::object bytes_of(const std::vector<uint8_t>& out) {
    return steal(PyBytes_FromStringAndSize(reinterpret_cast<const char*>(out.data()), static_cast<Py_ssize_t>(out.size())));
}

// --- Entry points -------------------------------------------------------------------------
// btoon/__init__.py validates the options and calls these positionally: pybind11's keyword
// matching doesn't survive an allocation failure (it segfaulted under fault injection).

py::object py_encode(py::handle obj, bool compress, py::handle compression, bool auto_tabular, size_t max_depth,
                     py::handle hmac_key, py::handle default_fn) {
    check_max_depth(max_depth);
    if (!default_fn.is_none() && !PyCallable_Check(default_fn.ptr())) raise_type("default must be callable or None", default_fn.ptr());
    EncodeOptions opts;
    opts.compress = compress;
    opts.compression_algorithm = parse_algorithm(compression.ptr());
    opts.auto_tabular = auto_tabular;
    opts.max_depth = max_depth;
    opts.hmac_key = key_from(hmac_key.ptr());
    opts.validate_utf8 = false;  // str -> UTF-8 via CPython is always well formed
    std::vector<uint8_t> out;
    try {
        Encoder enc(opts);
        PyEncoder walker(enc, max_depth, auto_tabular, default_fn.is_none() ? nullptr : default_fn.ptr());
        walker.encode(obj.ptr(), 0);
        out = enc.release();
        if (opts.compress || opts.hmac_key) {
            py::gil_scoped_release nogil;
            out = btoon::apply_envelopes(std::move(out), opts);
        }
    } catch (const BtoonException& e) {
        raise_from(e);
    }
    return bytes_of(out);
}

py::object py_decode(py::handle data, bool strict, size_t max_depth, size_t max_decompressed_size, size_t max_values,
                     bool auto_decompress, py::handle hmac_key, py::handle timestamps, py::handle vectors) {
    check_max_depth(max_depth);
    DecodeOptions opts;
    opts.strict = strict;
    opts.max_depth = max_depth;
    opts.max_decompressed_size = max_decompressed_size;
    opts.max_values = max_values;
    opts.auto_decompress = auto_decompress;
    opts.hmac_key = key_from(hmac_key.ptr());

    DecodeCtx ctx;
    ctx.strict = strict;
    if (is_str(timestamps.ptr(), "datetime")) ctx.timestamps = TimestampMode::datetime;
    else if (is_str(timestamps.ptr(), "object")) ctx.timestamps = TimestampMode::object;
    else raise_btoon("invalid_argument", "timestamps must be 'datetime' or 'object'", true);
    if (is_str(vectors.ptr(), "array")) ctx.vectors = VectorMode::array;
    else if (is_str(vectors.ptr(), "list")) ctx.vectors = VectorMode::list;
    else if (is_str(vectors.ptr(), "numpy")) ctx.vectors = VectorMode::numpy;
    else raise_btoon("invalid_argument", "vectors must be 'array', 'list' or 'numpy'", true);

    Value value;
    if (PyBytes_Check(data.ptr())) {
        // bytes are immutable, so the GIL can be released while decoding in place.
        std::span<const uint8_t> in(reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(data.ptr())),
                                    static_cast<size_t>(PyBytes_GET_SIZE(data.ptr())));
        try {
            py::gil_scoped_release nogil;
            value = btoon::decode(in, opts);
        } catch (const BtoonException& e) {
            raise_from(e);
        }
    } else {
        // Other buffers (bytearray, memoryview, ...) may change underneath us: copy first.
        std::vector<uint8_t> copy;
        {
            Buffer buf;
            if (!buf.acquire(data.ptr(), PyBUF_FULL_RO)) {
                if (!PyErr_ExceptionMatches(PyExc_TypeError)) throw py::error_already_set();
                PyErr_Clear();
                raise_type("expected a bytes-like object", data.ptr());
            }
            std::span<const uint8_t> in = buf.contiguous(copy);
            if (in.data() != copy.data()) copy.assign(in.begin(), in.end());
        }  // the export is released here, also if the copy above throws
        try {
            py::gil_scoped_release nogil;
            value = btoon::decode(copy, opts);
        } catch (const BtoonException& e) {
            raise_from(e);
        }
    }
    return to_py(value, ctx);
}

}  // namespace

PYBIND11_MODULE(_btoon, m, py::mod_gil_not_used()) {
    m.doc() = "Native core of the btoon package (use the btoon module, not this one).";
    PyDateTime_IMPORT;
    if (!PyDateTimeAPI) throw py::error_already_set();

    if (!C) {  // pybind11 runs this once per process, even if the module is imported again
        py::module_ types = py::module_::import("btoon._types");
        auto type_of = [&types](const char* name) {
            py::object t = types.attr(name);
            if (!PyType_Check(t.ptr())) throw py::type_error(std::string("btoon._types.") + name + " is not a class");
            return reinterpret_cast<PyTypeObject*>(new_ref(t));
        };
        auto* cache = new Cache{};
        cache->Timestamp = type_of("Timestamp");
        cache->Date = type_of("Date");
        cache->DateTime = type_of("DateTime");
        cache->ExtType = type_of("ExtType");
        cache->array = reinterpret_cast<PyTypeObject*>(new_ref(py::module_::import("array").attr("array")));
        cache->BtoonError = new_ref(types.attr("BtoonError"));
        cache->BtoonTypeError = new_ref(types.attr("BtoonTypeError"));
        cache->utc = new_ref(py::module_::import("datetime").attr("timezone").attr("utc"));
        cache->sys_modules = new_ref(py::module_::import("sys").attr("modules"));
        cache->int_from_bytes = new_ref(py::module_::import("builtins").attr("int").attr("from_bytes"));
        cache->int_to_bytes = new_ref(py::module_::import("builtins").attr("int").attr("to_bytes"));
        cache->seconds = intern("seconds");
        cache->nanoseconds = intern("nanoseconds");
        cache->offset_minutes = intern("offset_minutes");
        cache->epoch_milliseconds = intern("epoch_milliseconds");
        cache->epoch_nanoseconds = intern("epoch_nanoseconds");
        cache->code = intern("code");
        cache->data = intern("data");
        cache->utcoffset = intern("utcoffset");
        cache->tolist = intern("tolist");
        cache->bit_length = intern("bit_length");
        cache->big = intern("big");
        cache->frombytes = intern("frombytes");
        cache->numpy = intern("numpy");
        cache->numpy_ma = intern("numpy.ma");
        cache->ndarray = intern("ndarray");
        cache->MaskedArray = intern("MaskedArray");
        cache->signed_kwnames = new_ref(py::make_tuple("signed"));
        C = cache;
    }

    m.def("encode", &py_encode, py::arg("obj"), py::arg("compress"), py::arg("compression"), py::arg("auto_tabular"),
          py::arg("max_depth"), py::arg("hmac_key"), py::arg("default"));
    m.def("decode", &py_decode, py::arg("data"), py::arg("strict"), py::arg("max_depth"), py::arg("max_decompressed_size"),
          py::arg("max_values"), py::arg("auto_decompress"), py::arg("hmac_key"), py::arg("timestamps"), py::arg("vectors"));
    m.def("compression_available", [](py::handle name) {
        CompressionAlgorithm algorithm = parse_algorithm(name.ptr());
        // "auto" picks among the built-in algorithms (zlib at least), so it always works.
        return algorithm == CompressionAlgorithm::AUTO || btoon::compression_available(algorithm);
    });
    m.attr("CORE_VERSION") = btoon::version();
    m.attr("SPEC_VERSION") = btoon::spec_version();
    // Core's limits (SPEC §7, §7.1), so the Python defaults can't drift from them.
    m.attr("DEFAULT_MAX_DEPTH") = btoon::kDefaultMaxDepth;
    m.attr("MAX_DEPTH_LIMIT") = btoon::kMaxDepthLimit;
    m.attr("DEFAULT_MAX_DECOMPRESSED_SIZE") = btoon::kDefaultMaxDecompressedSize;
    m.attr("DEFAULT_MAX_VALUES") = btoon::kDefaultMaxValues;
}
