// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// CPython extension for BTOON. Python objects are converted to a btoon::Value
// (with the GIL held, depth-limited so cycles fail cleanly), encoded by the
// C++ core with the GIL released, and vice versa for decoding. All parsing of
// untrusted bytes happens in btoon-core's hardened decoder.
#include <pybind11/pybind11.h>

#include <datetime.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "btoon/btoon.h"
#include "btoon/encoder.h"

namespace py = pybind11;
using namespace btoon;

namespace {

// Python objects cached at import time. Intentionally leaked: they live as long
// as the interpreter and must not be destroyed after it finalizes.
struct Cache {
    py::object Timestamp, ExtType, BtoonError, BtoonTypeError;
    py::object utc, timezone_cls, timedelta_cls;
    py::object array_cls;           // array.array
    py::object sys_modules;
};
Cache* C = nullptr;

[[noreturn]] void raise_btoon(const char* code, const std::string& message, bool type_error = false) {
    py::object exc = (type_error ? C->BtoonTypeError : C->BtoonError)(code, message);
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exc.ptr())), exc.ptr());
    throw py::error_already_set();
}

[[noreturn]] void raise_from(const BtoonException& e) { raise_btoon(e.code_name(), e.what()); }

// --- civil date helpers (proleptic Gregorian) --------------------------------------
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

// --- Python -> bytes (direct, no intermediate Value tree) ---------------------------------

Timestamp datetime_to_timestamp(PyObject* o) {
    int64_t days = days_from_civil(PyDateTime_GET_YEAR(o), PyDateTime_GET_MONTH(o), PyDateTime_GET_DAY(o));
    int64_t secs = days * 86400 + PyDateTime_DATE_GET_HOUR(o) * 3600LL + PyDateTime_DATE_GET_MINUTE(o) * 60LL +
                   PyDateTime_DATE_GET_SECOND(o);
    uint32_t nanos = static_cast<uint32_t>(PyDateTime_DATE_GET_MICROSECOND(o)) * 1000u;
    // Aware datetimes are normalized to UTC; naive ones are taken to be UTC already.
    py::object off = py::reinterpret_borrow<py::object>(o).attr("utcoffset")();
    if (!off.is_none()) {
        PyObject* td = off.ptr();
        // A datetime subclass can override utcoffset() to return anything. Reading a
        // non-timedelta with the PyDateTime_DELTA_* macros is a heap over-read, and a huge
        // timedelta overflows the arithmetic below. CPython's own datetime enforces the
        // same rules.
        if (!PyDelta_Check(td)) {
            raise_btoon("invalid_argument",
                        std::string("utcoffset() must return a timedelta or None, not ") + Py_TYPE(td)->tp_name,
                        true);
        }
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

class PyEncoder {
public:
    PyEncoder(Encoder& enc, size_t max_depth, py::object default_fn)
        : enc_(enc), max_depth_(max_depth), default_(std::move(default_fn)) {}

    void encode(PyObject* o, size_t depth) {
        if (o == Py_None) return enc_.encodeNil();
        if (o == Py_True) return enc_.encodeBool(true);
        if (o == Py_False) return enc_.encodeBool(false);
        if (PyLong_Check(o)) return encode_int(o);
        if (PyFloat_Check(o)) return enc_.encodeFloat(PyFloat_AS_DOUBLE(o));
        if (PyUnicode_Check(o)) return enc_.encodeString(utf8_of(o));
        if (PyBytes_Check(o)) {
            return enc_.encodeBinary({reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(o)),
                                      static_cast<size_t>(PyBytes_GET_SIZE(o))});
        }
        if (PyTuple_Check(o) && !PyTuple_CheckExact(o) && PyObject_IsInstance(o, C->ExtType.ptr()) == 1) {
            // ExtType is a namedtuple: recognize it before treating tuples as sequences.
            py::handle h(o);
            py::bytes data = h.attr("data");
            return enc_.encodeExtension(h.attr("code").cast<int8_t>(),
                                        {reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(data.ptr())),
                                         static_cast<size_t>(PyBytes_GET_SIZE(data.ptr()))});
        }
        if (PyList_Check(o) || PyTuple_Check(o)) return encode_sequence(o, depth);
        if (PyDict_Check(o)) return encode_dict(o, depth);
        if (PyDateTime_Check(o)) return enc_.encodeTimestamp(datetime_to_timestamp(o));
        if (PyByteArray_Check(o) || PyMemoryView_Check(o)) return encode_buffer(o);
        py::handle h(o);
        if (py::isinstance(h, C->Timestamp)) {
            py::object off = h.attr("offset_minutes");
            auto secs = h.attr("seconds").cast<int64_t>();
            auto nanos = h.attr("nanoseconds").cast<uint32_t>();
            return enc_.encodeTimestamp(off.is_none() ? Timestamp(secs, nanos) : Timestamp(secs, nanos, off.cast<int16_t>()));
        }
        if (py::isinstance(h, C->array_cls)) {
            std::string tc = h.attr("typecode").cast<std::string>();
            if (tc == "f" || tc == "d") return encode_float_buffer(o, tc == "f");
            return encode(h.attr("tolist")().ptr(), depth);
        }
        if (encode_numpy(h, depth)) return;
        if (!default_.is_none()) {
            check_depth(depth);  // a default() that keeps returning unknown objects terminates
            py::object replacement = default_(h);
            return encode(replacement.ptr(), depth + 1);
        }
        raise_btoon("invalid_argument",
                    std::string("object of type '") + Py_TYPE(o)->tp_name + "' is not BTOON serializable", true);
    }

private:
    void check_depth(size_t depth) const {
        if (depth >= max_depth_) raise_btoon("max_depth", "maximum nesting depth exceeded (or the object contains a cycle)");
    }

    void encode_int(PyObject* o) {
        int overflow = 0;
        long long v = PyLong_AsLongLongAndOverflow(o, &overflow);
        if (v == -1 && PyErr_Occurred()) throw py::error_already_set();
        if (!overflow) return enc_.encodeInt(v);
        if (overflow > 0) {
            unsigned long long u = PyLong_AsUnsignedLongLong(o);
            if (!(u == static_cast<unsigned long long>(-1) && PyErr_Occurred())) return enc_.encodeUint(u);
            PyErr_Clear();
        }
        // Arbitrary precision: two's complement big-endian bytes (SPEC §5.4).
        py::handle h(o);
        size_t bits = h.attr("bit_length")().cast<size_t>();
        py::bytes b = h.attr("to_bytes")((bits + 8) / 8, "big", py::arg("signed") = true);
        enc_.encodeBigInt({reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(b.ptr())),
                           static_cast<size_t>(PyBytes_GET_SIZE(b.ptr()))});
    }

    void encode_buffer(PyObject* o) {
        Py_buffer view;
        if (PyObject_GetBuffer(o, &view, PyBUF_CONTIG_RO) != 0) throw py::error_already_set();
        try {
            enc_.encodeBinary({static_cast<const uint8_t*>(view.buf), static_cast<size_t>(view.len)});
        } catch (...) {
            PyBuffer_Release(&view);
            throw;
        }
        PyBuffer_Release(&view);
    }

    void encode_float_buffer(PyObject* o, bool f32) {
        Py_buffer view;
        if (PyObject_GetBuffer(o, &view, PyBUF_CONTIG_RO) != 0) throw py::error_already_set();
        try {
            if (f32) {
                enc_.encodeVectorFloat({static_cast<const float*>(view.buf), static_cast<size_t>(view.len) / sizeof(float)});
            } else {
                enc_.encodeVectorDouble({static_cast<const double*>(view.buf), static_cast<size_t>(view.len) / sizeof(double)});
            }
        } catch (...) {
            PyBuffer_Release(&view);
            throw;
        }
        PyBuffer_Release(&view);
    }

    // numpy without a compile-time dependency: 1-D C-contiguous native float32/float64 arrays
    // become float vectors; any other ndarray is encoded via tolist().
    bool encode_numpy(py::handle h, size_t depth) {
        if (!C->sys_modules.contains("numpy")) return false;
        py::object np = C->sys_modules["numpy"];
        if (!py::isinstance(h, np.attr("ndarray"))) return false;
        py::object dtype = h.attr("dtype");
        if (h.attr("ndim").cast<int>() == 1 && h.attr("flags").attr("c_contiguous").cast<bool>() &&
            dtype.attr("byteorder").cast<std::string>() != ">") {
            std::string name = dtype.attr("name").cast<std::string>();
            if (name == "float32" || name == "float64") {
                encode_float_buffer(h.ptr(), name == "float32");
                return true;
            }
        }
        encode(h.attr("tolist")().ptr(), depth);
        return true;
    }

    struct Entry {
        std::string_view key;
        PyObject* key_obj;
        PyObject* value;
    };

    // Collects (utf8 key, key, value) for a dict, sorted by UTF-8 bytes (canonical order).
    // `items` keeps strong references alive for the duration of the call.
    void sorted_entries(PyObject* dict, py::list& items, std::vector<Entry>& out) {
        items = py::reinterpret_steal<py::list>(PyDict_Items(dict));
        if (!items) throw py::error_already_set();
        out.clear();
        out.reserve(static_cast<size_t>(PyList_GET_SIZE(items.ptr())));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(items.ptr()); ++i) {
            PyObject* pair = PyList_GET_ITEM(items.ptr(), i);
            PyObject* k = PyTuple_GET_ITEM(pair, 0);
            if (!PyUnicode_Check(k)) {
                raise_btoon("invalid_argument", std::string("map keys must be str, not ") + Py_TYPE(k)->tp_name, true);
            }
            out.push_back({utf8_of(k), k, PyTuple_GET_ITEM(pair, 1)});
        }
        std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
    }

    void encode_dict(PyObject* o, size_t depth) {
        check_depth(depth);
        py::list items;
        std::vector<Entry> entries;
        sorted_entries(o, items, entries);
        enc_.encodeMapHeader(entries.size());
        for (const Entry& e : entries) {
            enc_.encodeString(e.key);
            encode(e.value, depth + 1);
        }
    }

    void encode_sequence(PyObject* o, size_t depth) {
        check_depth(depth);
        // Snapshot lists (tuples are immutable): user code (default(), tzinfo, ...) may run mid-encode.
        py::object snapshot = PyTuple_CheckExact(o) ? py::reinterpret_borrow<py::object>(o)
                                                    : py::reinterpret_steal<py::object>(PySequence_Tuple(o));
        if (!snapshot) throw py::error_already_set();
        PyObject* t = snapshot.ptr();
        const Py_ssize_t n = PyTuple_GET_SIZE(t);
        if (options_tabular_ && try_tabular(t, n, depth)) return;
        enc_.encodeArrayHeader(static_cast<size_t>(n));
        for (Py_ssize_t i = 0; i < n; ++i) encode(PyTuple_GET_ITEM(t, i), depth + 1);
    }

    // Same qualification as SPEC §5.6.2: ≥ 2 rows, all exact-or-sub dicts, same non-empty key set.
    bool try_tabular(PyObject* rows, Py_ssize_t n, size_t depth) {
        if (n < 2) return false;
        PyObject* first = PyTuple_GET_ITEM(rows, 0);
        if (!PyDict_Check(first)) return false;
        const Py_ssize_t c = PyDict_GET_SIZE(first);
        if (c == 0) return false;
        for (Py_ssize_t r = 1; r < n; ++r) {
            PyObject* row = PyTuple_GET_ITEM(rows, r);
            if (!PyDict_Check(row) || PyDict_GET_SIZE(row) != c) return false;
        }
        py::list items;
        std::vector<Entry> cols;
        PyObject *k, *v;
        Py_ssize_t pos = 0;
        while (PyDict_Next(first, &pos, &k, &v)) {
            if (!PyUnicode_Check(k)) return false;  // the plain path reports the error
        }
        sorted_entries(first, items, cols);
        for (Py_ssize_t r = 1; r < n; ++r) {
            PyObject* row = PyTuple_GET_ITEM(rows, r);
            for (const Entry& e : cols) {
                PyObject* got = PyDict_GetItemWithError(row, e.key_obj);
                if (!got) {
                    if (PyErr_Occurred()) throw py::error_already_set();
                    return false;
                }
            }
        }
        std::vector<size_t> lengths;
        std::vector<std::string_view> names;
        lengths.reserve(cols.size());
        names.reserve(cols.size());
        for (const Entry& e : cols) {
            lengths.push_back(e.key.size());
            names.push_back(e.key);
        }
        if (!btoon::tabular_beneficial(static_cast<size_t>(n), lengths)) return false;
        if (depth + 1 >= max_depth_) raise_btoon("max_depth", "maximum nesting depth exceeded (or the object contains a cycle)");

        // Hold the cells of every row (strong refs) so user code can't free them mid-encode.
        std::vector<py::object> cells(static_cast<size_t>(n) * cols.size());
        for (Py_ssize_t r = 0; r < n; ++r) {
            PyObject* row = PyTuple_GET_ITEM(rows, r);
            for (size_t col = 0; col < cols.size(); ++col) {
                PyObject* cell = PyDict_GetItemWithError(row, cols[col].key_obj);
                if (!cell) {
                    if (PyErr_Occurred()) throw py::error_already_set();
                    raise_btoon("invalid_argument", "dict changed size during encoding", true);
                }
                cells[static_cast<size_t>(r) * cols.size() + col] = py::reinterpret_borrow<py::object>(cell);
            }
        }
        enc_.beginTabular(names, static_cast<size_t>(n));
        for (size_t col = 0; col < cols.size(); ++col) {
            enc_.beginTabularColumn();
            for (Py_ssize_t r = 0; r < n; ++r) encode(cells[static_cast<size_t>(r) * cols.size() + col].ptr(), depth + 2);
            enc_.endTabularColumn();
        }
        enc_.endTabular();
        return true;
    }

public:
    bool options_tabular_ = true;

private:
    Encoder& enc_;
    size_t max_depth_;
    py::object default_;
};

// --- Value -> Python -------------------------------------------------------------------

enum class TimestampMode { datetime, object };
enum class VectorMode { array, list, numpy };

struct DecodeCtx {
    TimestampMode timestamps;
    VectorMode vectors;
    bool strict;
    // Repeated keys (e.g. every row of a table) share one str object.
    std::unordered_map<std::string_view, py::object> keys;
    std::unordered_map<int, py::object> zones;
};

py::object key_object(const std::string& k, DecodeCtx& ctx) {
    if (k.size() > 64) return py::reinterpret_steal<py::object>(PyUnicode_DecodeUTF8(k.data(), static_cast<Py_ssize_t>(k.size()), ctx.strict ? "strict" : "surrogateescape"));
    auto it = ctx.keys.find(k);
    if (it != ctx.keys.end()) return it->second;
    PyObject* s = PyUnicode_DecodeUTF8(k.data(), static_cast<Py_ssize_t>(k.size()), ctx.strict ? "strict" : "surrogateescape");
    if (!s) throw py::error_already_set();
    py::object obj = py::reinterpret_steal<py::object>(s);
    if (ctx.keys.size() < 8192) ctx.keys.emplace(std::string_view(k), obj);
    return obj;
}

py::object timestamp_object(int64_t seconds, uint32_t nanos, std::optional<int16_t> offset) {
    py::object off = offset ? py::object(py::int_(*offset)) : py::object(py::none());
    return C->Timestamp(seconds, nanos, off);
}

// Aware datetime (microsecond precision). Out-of-range instants fall back to btoon.Timestamp.
py::object instant_to_py(int64_t seconds, uint32_t nanos, std::optional<int16_t> offset, DecodeCtx& ctx) {
    if (ctx.timestamps == TimestampMode::object) return timestamp_object(seconds, nanos, offset);
    int64_t local = seconds + (offset ? *offset * 60LL : 0);
    int64_t days = local >= 0 ? local / 86400 : (local - 86399) / 86400;
    int64_t sod = local - days * 86400;
    int64_t y;
    unsigned mo, d;
    civil_from_days(days, y, mo, d);
    if (y < 1 || y > 9999) return timestamp_object(seconds, nanos, offset);
    py::object tz = C->utc;
    if (offset) {
        auto it = ctx.zones.find(*offset);
        if (it == ctx.zones.end()) {
            py::object zone = C->timezone_cls(C->timedelta_cls(py::arg("minutes") = *offset));
            it = ctx.zones.emplace(*offset, zone).first;
        }
        tz = it->second;
    }
    PyObject* dt = PyDateTimeAPI->DateTime_FromDateAndTime(
        static_cast<int>(y), static_cast<int>(mo), static_cast<int>(d), static_cast<int>(sod / 3600),
        static_cast<int>(sod / 60 % 60), static_cast<int>(sod % 60), static_cast<int>(nanos / 1000), tz.ptr(),
        PyDateTimeAPI->DateTimeType);
    if (!dt) throw py::error_already_set();
    return py::reinterpret_steal<py::object>(dt);
}

template <class T>
py::object vector_to_py(const std::vector<T>& data, const char* typecode, const char* dtype, DecodeCtx& ctx) {
    py::bytes raw(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(T));
    switch (ctx.vectors) {
        case VectorMode::list: {
            py::list l(data.size());
            for (size_t i = 0; i < data.size(); ++i) l[i] = py::float_(static_cast<double>(data[i]));
            return std::move(l);
        }
        case VectorMode::numpy: {
            py::object np = py::module_::import("numpy");
            return np.attr("frombuffer")(raw, py::arg("dtype") = dtype).attr("copy")();
        }
        case VectorMode::array:
        default: {
            py::object a = C->array_cls(typecode);
            a.attr("frombytes")(raw);
            return a;
        }
    }
}

py::object to_py(const Value& v, DecodeCtx& ctx) {
    switch (v.index()) {
        case 0: return py::none();
        case 1: return py::bool_(std::get<Bool>(v));
        case 2: return py::reinterpret_steal<py::object>(PyLong_FromLongLong(std::get<Int>(v)));
        case 3: return py::reinterpret_steal<py::object>(PyLong_FromUnsignedLongLong(std::get<Uint>(v)));
        case 4: return py::reinterpret_steal<py::object>(PyFloat_FromDouble(std::get<Float>(v)));
        case 5: {
            const auto& s = std::get<String>(v);
            PyObject* u = PyUnicode_DecodeUTF8(s.data(), static_cast<Py_ssize_t>(s.size()), ctx.strict ? "strict" : "surrogateescape");
            if (!u) throw py::error_already_set();
            return py::reinterpret_steal<py::object>(u);
        }
        case 6: {
            const auto& b = std::get<Binary>(v);
            return py::bytes(reinterpret_cast<const char*>(b.data()), b.size());
        }
        case 7: {
            const auto& a = std::get<Array>(v);
            PyObject* list = PyList_New(static_cast<Py_ssize_t>(a.size()));
            if (!list) throw py::error_already_set();
            py::object guard = py::reinterpret_steal<py::object>(list);
            for (size_t i = 0; i < a.size(); ++i) {
                PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), to_py(a[i], ctx).release().ptr());
            }
            return guard;
        }
        case 8: {
            const auto& m = std::get<Map>(v);
            py::dict d;
            for (const auto& [k, e] : m) {
                py::object val = to_py(e, ctx);
                if (PyDict_SetItem(d.ptr(), key_object(k, ctx).ptr(), val.ptr()) != 0) throw py::error_already_set();
            }
            return std::move(d);
        }
        case 9: {
            const auto& e = std::get<Extension>(v);
            return C->ExtType(e.type, py::bytes(reinterpret_cast<const char*>(e.data.data()), e.data.size()));
        }
        case 10: {
            const auto& ts = std::get<Timestamp>(v);
            std::optional<int16_t> off;
            if (ts.has_timezone) off = ts.timezone_offset;
            return instant_to_py(ts.seconds, ts.nanoseconds, off, ctx);
        }
        case 11: {
            int64_t ms = std::get<Date>(v).milliseconds;
            int64_t s = ms / 1000, r = ms % 1000;
            if (r < 0) { r += 1000; --s; }
            return instant_to_py(s, static_cast<uint32_t>(r * 1000000), std::nullopt, ctx);
        }
        case 12: {
            int64_t ns = std::get<DateTime>(v).nanoseconds;
            int64_t s = ns / 1000000000, r = ns % 1000000000;
            if (r < 0) { r += 1000000000; --s; }
            return instant_to_py(s, static_cast<uint32_t>(r), std::nullopt, ctx);
        }
        case 13: {
            const auto& b = std::get<BigInt>(v).bytes;
            py::bytes raw(reinterpret_cast<const char*>(b.data()), b.size());
            return py::module_::import("builtins").attr("int").attr("from_bytes")(raw, "big", py::arg("signed") = true);
        }
        case 14: return vector_to_py(std::get<VectorFloat>(v).data, "f", "<f4", ctx);
        case 15: return vector_to_py(std::get<VectorDouble>(v).data, "d", "<f8", ctx);
    }
    return py::none();
}

CompressionAlgorithm parse_algorithm(const std::string& name) {
    if (name == "zlib") return CompressionAlgorithm::ZLIB;
    if (name == "lz4") return CompressionAlgorithm::LZ4;
    if (name == "zstd") return CompressionAlgorithm::ZSTD;
    if (name == "brotli") return CompressionAlgorithm::BROTLI;
    if (name == "snappy") return CompressionAlgorithm::SNAPPY;
    if (name == "auto") return CompressionAlgorithm::AUTO;
    raise_btoon("invalid_argument", "unknown compression algorithm: '" + name + "'", true);
}

std::string key_from(const py::object& key) {
    if (key.is_none()) return {};
    std::string k;
    if (py::isinstance<py::str>(key) || py::isinstance<py::bytes>(key)) {
        k = key.cast<std::string>();
    } else {
        raise_btoon("invalid_argument", "hmac_key must be bytes or str", true);
    }
    // An empty key is a misconfiguration, not "no key" (SPEC §8): never silently skip verification.
    if (k.empty()) raise_btoon("invalid_argument", "hmac_key must not be empty (pass None for no key)", true);
    return k;
}

// --- Entry points -------------------------------------------------------------------------

py::bytes py_encode(py::handle obj, bool compress, const std::string& compression, bool auto_tabular,
                    size_t max_depth, py::object hmac_key, py::object default_fn) {
    EncodeOptions opts;
    opts.compress = compress;
    opts.compression_algorithm = parse_algorithm(compression);
    opts.auto_tabular = auto_tabular;
    opts.max_depth = max_depth;
    opts.hmac_key = key_from(hmac_key);
    opts.validate_utf8 = false;  // str -> UTF-8 via CPython is always well formed
    std::vector<uint8_t> out;
    try {
        Encoder enc(opts);
        PyEncoder walker(enc, max_depth, std::move(default_fn));
        walker.options_tabular_ = auto_tabular;
        walker.encode(obj.ptr(), 0);
        out = enc.release();
        if (opts.compress || !opts.hmac_key.empty()) {
            py::gil_scoped_release nogil;
            out = btoon::apply_envelopes(std::move(out), opts);
        }
    } catch (const BtoonException& e) {
        raise_from(e);
    }
    return py::bytes(reinterpret_cast<const char*>(out.data()), out.size());
}

py::object py_decode(py::handle data, bool strict, size_t max_depth, size_t max_decompressed_size, bool auto_decompress,
                  py::object hmac_key, const std::string& timestamps, const std::string& vectors) {
    DecodeOptions opts;
    opts.strict = strict;
    opts.max_depth = max_depth;
    opts.max_decompressed_size = max_decompressed_size;
    opts.auto_decompress = auto_decompress;
    opts.hmac_key = key_from(hmac_key);

    DecodeCtx ctx;
    ctx.strict = strict;
    if (timestamps == "datetime") ctx.timestamps = TimestampMode::datetime;
    else if (timestamps == "object") ctx.timestamps = TimestampMode::object;
    else raise_btoon("invalid_argument", "timestamps must be 'datetime' or 'object'", true);
    if (vectors == "array") ctx.vectors = VectorMode::array;
    else if (vectors == "list") ctx.vectors = VectorMode::list;
    else if (vectors == "numpy") ctx.vectors = VectorMode::numpy;
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
        Py_buffer view;
        if (PyObject_GetBuffer(data.ptr(), &view, PyBUF_CONTIG_RO) != 0) {
            PyErr_Clear();
            raise_btoon("invalid_argument", std::string("expected a bytes-like object, not ") + Py_TYPE(data.ptr())->tp_name, true);
        }
        std::vector<uint8_t> copy(static_cast<const uint8_t*>(view.buf), static_cast<const uint8_t*>(view.buf) + view.len);
        PyBuffer_Release(&view);
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

PYBIND11_MODULE(_btoon, m) {
    m.doc() = "Native core of the btoon package (use the btoon module, not this one).";
    PyDateTime_IMPORT;
    if (!PyDateTimeAPI) throw py::error_already_set();

    py::module_ types = py::module_::import("btoon._types");
    py::module_ datetime = py::module_::import("datetime");
    C = new Cache{types.attr("Timestamp"),
                  types.attr("ExtType"),
                  types.attr("BtoonError"),
                  types.attr("BtoonTypeError"),
                  datetime.attr("timezone").attr("utc"),
                  datetime.attr("timezone"),
                  datetime.attr("timedelta"),
                  py::module_::import("array").attr("array"),
                  py::module_::import("sys").attr("modules")};

    m.def("encode", &py_encode, py::arg("obj"), py::kw_only(), py::arg("compress"), py::arg("compression"),
          py::arg("auto_tabular"), py::arg("max_depth"), py::arg("hmac_key"), py::arg("default"));
    m.def("decode", &py_decode, py::arg("data"), py::kw_only(), py::arg("strict"), py::arg("max_depth"),
          py::arg("max_decompressed_size"), py::arg("auto_decompress"), py::arg("hmac_key"), py::arg("timestamps"),
          py::arg("vectors"));
    m.def("compression_available", [](const std::string& name) { return btoon::compression_available(parse_algorithm(name)); });
    m.attr("CORE_VERSION") = btoon::version();
    m.attr("SPEC_VERSION") = btoon::spec_version();
}
