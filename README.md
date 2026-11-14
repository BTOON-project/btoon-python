# btoon for Python

[![PyPI](https://img.shields.io/pypi/v/btoon.svg)](https://pypi.org/project/btoon/)
[![Python](https://img.shields.io/pypi/pyversions/btoon.svg)](https://pypi.org/project/btoon/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Python bindings for **BTOON**, a compact binary serialization format: **MessagePack plus a
columnar encoding for lists of uniform dicts**, with timestamps, dates, arbitrary-size
integers, float vectors and optional compression/signing. It runs on the C++ reference
implementation ([btoon-core](https://github.com/BTOON-project/btoon-core)) and implements
spec 0.3. Wheels cover CPython 3.10 to 3.15, including the free-threaded 3.14t and 3.15t.

```bash
pip install btoon
```

## Quick start

```python
import btoon

rows = [{"id": i, "name": f"user{i}", "email": f"u{i}@example.com", "active": i % 2 == 0} for i in range(1000)]

data = btoon.encode(rows)                 # bytes; tables are stored column by column
assert btoon.decode(data) == rows

packed = btoon.encode(rows, compress=True)   # zlib envelope, detected on decode
signed = btoon.encode(rows, hmac_key=b"32 bytes of secret key material!")
btoon.decode(signed, hmac_key=b"32 bytes of secret key material!")
```

## Why

For record-shaped data the column layout skips repeating keys. Measured with Python 3.13
on an Apple M4 Max, 10,000 records (`id`, `name`, `email`, `age`, `score`, `active`), best
of 7:

| | Size | Encode | Decode |
|---|---:|---:|---:|
| `json` | 1,138 KB | 4.16 ms | 3.68 ms |
| `msgpack` 1.2 (C extension) | 767 KB | 1.81 ms | 3.20 ms |
| **`btoon`** | **447 KB** | **0.77 ms** | **3.15 ms** |

For data that isn't tabular (nested and irregular), BTOON is byte-for-byte canonical
MessagePack, and its speed is close to msgpack's: 2.54 ms encode / 4.95 ms decode vs
2.42 / 4.32 ms on a comparable dataset. Sizes and timings depend on your data. Measure
with your own.

## Types

| Python → BTOON → Python | Notes |
|---|---|
| `None`, `bool`, `float`, `str` | float is always float64; NaN, ±inf and -0.0 are kept |
| `int` | Any size. Outside 64 bits it uses the bigint extension. |
| `bytes`, `bytearray`, `memoryview` → `bytes` | |
| `list`, `tuple` → `list` | Lists of dicts with identical keys go tabular automatically when that is smaller |
| `dict` (str keys) → `dict` | Keys are written in sorted (UTF-8 byte) order |
| `datetime` → aware `datetime` | A timestamp. Naive values are taken as UTC. Decoding has microsecond precision. |
| `btoon.Timestamp` | Timestamp with nanosecond precision and an optional UTC offset |
| `btoon.Date` | Date: milliseconds since the epoch, e.g. `btoon.Date(1700000000123)` |
| `btoon.DateTime` | Datetime: nanoseconds since the epoch (1677 to 2262) |
| `array.array('f'/'d')`, 1-D float32/float64 `numpy` arrays → `array.array` | Packed float vectors, e.g. embeddings. Any strides or byte order. Use `vectors="list"` or `"numpy"` to change the result type. Other numpy arrays are encoded via `tolist()`. |
| `btoon.ExtType(code, data)` | Any other extension type. Codes -1 to -6 and -10 are BTOON's own types, written from the types above: encoding them raw raises `BtoonError` (`invalid_argument`). |

Timestamps, dates and datetimes decode to aware `datetime` objects by default. With
`decode(..., timestamps="object")` they decode to `btoon.Timestamp`, `btoon.Date` and
`btoon.DateTime`, which keep every digit and encode back to the same bytes:

```python
data = btoon.encode({"day": btoon.Date(1700000000123), "at": btoon.DateTime(1700000000123456789)})
btoon.decode(data)                       # {'at': datetime(2023, 11, 14, 22, 13, 20, 123456, tzinfo=utc), ...}
value = btoon.decode(data, timestamps="object")
value["day"]                             # btoon.Date(1700000000123)
assert btoon.encode(value) == data
btoon.Date.from_datetime(datetime.datetime.now(datetime.timezone.utc))   # rounds down to the millisecond
```

`Date` and `DateTime` are subclasses of `Timestamp` (so `seconds` and `nanoseconds` work),
but they compare unequal to it: `btoon.Date(0) != btoon.Timestamp(0)`, because they are
different BTOON values. Both are frozen dataclasses. `dataclasses.replace()` and
`copy.replace()` take only the epoch value, e.g. `replace(d, epoch_milliseconds=5)`. An
instant outside `datetime`'s years 1–9999 decodes as `Timestamp`, `Date` or `DateTime`
even in the default mode.

Other objects raise `TypeError` unless you pass `default=` (like `json.dumps`). Its
return value takes the object's place, at the same depth, and objects it turns into
dicts can form a table like any list of dicts:

```python
btoon.encode(obj, default=lambda o: o.__dict__)
```

numpy masked arrays are refused (they would lose their mask); convert them first, for
example with `.filled(...)` or `.tolist()`. Self-referencing structures raise
`btoon.BtoonError(code="max_depth")` instead of crashing.

## Options

```python
btoon.encode(obj, *, compress=False, compression="zlib", auto_tabular=True,
             max_depth=128, hmac_key=None, default=None)
btoon.decode(data, *, strict=True, max_depth=128, max_decompressed_size=64 * 2**20,
             max_values=2**20, auto_decompress=True, hmac_key=None,
             timestamps="datetime", vectors="array")
btoon.compression_available("zstd")   # zlib (and "auto", which picks one) are always available
```

`max_depth` is at most 1000. `hmac_key=None` means no key. An empty key raises
`BtoonTypeError` (code `invalid_argument`) instead of turning signing or verification off.

## Untrusted input

`decode` is safe on hostile bytes. It is bounds-checked and depth-limited, it never
allocates more than the input justifies, it caps decompression, and in strict mode it
rejects invalid UTF-8, duplicate keys and trailing data. Map keys and column names must be
valid UTF-8 even with `strict=False`.

Two limits bound the memory a document can claim (spec §7.1):

- `max_decompressed_size` (64 MiB by default) caps what a compression envelope expands
  to, and the column-name bytes a table copies into its rows (rows × total name length).
- `max_values` (2**20 = 1,048,576 by default) caps how many values a document decodes to.
  The top-level value, every list item and dict value, and every table row and cell count
  once. Dict keys don't count.

Going over either raises `size_limit`. Raise them to decode bigger documents, and keep the
defaults (or lower them) for untrusted input. Every failure raises `btoon.BtoonError` (a
`ValueError`) whose `.code` is one of the spec's error codes:

```python
try:
    btoon.decode(payload, max_depth=32, max_decompressed_size=4 << 20, max_values=100_000)
except btoon.BtoonError as e:
    print(e.code, e)    # e.g. "truncated  unexpected end of input (at byte 7)"
```

Parsing during `decode`, and compression and signing during `encode`, run with the GIL
released, so other threads keep running. Converting to and from Python objects holds
the GIL. While it converts, `decode` frees the parts of the parsed document it has
already turned into Python objects, instead of keeping all of it until the end. Where the
C allocator can reuse or return that memory, as with long strings and binary values, it
no longer counts twice toward the peak.

## Threads

`encode` and `decode` can be called from any number of threads. On free-threaded CPython
(3.14t and later) the extension runs without the GIL, so they run in parallel. Each list
or dict is encoded from one consistent snapshot, even while another thread changes it.
Changing a bytearray or numpy array while it is being encoded gives unspecified values for
those elements, but never a crash.

## Interoperability

`btoon.decode` reads any MessagePack whose map keys are strings, including MessagePack
timestamps. Output with `auto_tabular=False` is canonical MessagePack, but BTOON's own
types are extensions with negative type codes: bigints (-4), dates (-2), datetimes (-3),
float vectors (-5, -6), and, with tabular encoding on, tables (-10). Some MessagePack
libraries need help with those. msgpack-python, for example, reads documents without
them directly, but needs an `ext_hook`, because its default `ExtType` refuses negative
codes:

```python
import msgpack
msgpack.unpackb(btoon.encode({"n": 2**70}), ext_hook=lambda code, data: (code, data))
# {'n': (-4, b'@\x00\x00\x00\x00\x00\x00\x00\x00')}
```

A `btoon.Timestamp` with a UTC offset uses BTOON's 14-byte timestamp form, which
msgpack-python rejects even with an `ext_hook`. The same bytes decode identically in the
[Node.js](https://github.com/BTOON-project/btoon-nodejs),
[JavaScript](https://github.com/BTOON-project/btoon-javascript),
[Go](https://github.com/BTOON-project/btoon-go) and
[PHP](https://github.com/BTOON-project/btoon-php) libraries.

## Building from source

```bash
git clone --recursive https://github.com/BTOON-project/btoon-python
cd btoon-python
pip install ".[test]" && pytest
```

You need a C++20 compiler and CMake ≥ 3.20 (pip fetches scikit-build-core and pybind11).
zlib comes from the system, except on Windows, where it is compiled in. When developing
next to a btoon-core checkout, the build picks up `../btoon-core` automatically if
`core/` is empty. You can also point it elsewhere with `BTOON_CORE_DIR=/path/to/btoon-core`.

## License

MIT
