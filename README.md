# btoon for Python

[![PyPI](https://img.shields.io/pypi/v/btoon.svg)](https://pypi.org/project/btoon/)
[![Python](https://img.shields.io/pypi/pyversions/btoon.svg)](https://pypi.org/project/btoon/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Python bindings for **BTOON**, a compact binary serialization format: **MessagePack plus a
columnar encoding for lists of uniform dicts**, with timestamps, arbitrary-size integers,
float vectors and optional compression/signing. It runs on the C++ reference
implementation ([btoon-core](https://github.com/BTOON-project/btoon-core)) and implements
spec 0.1.

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
| `datetime` → aware `datetime` | Naive values are taken as UTC. Decoding has microsecond precision. |
| `btoon.Timestamp` | Nanosecond precision and an optional UTC offset. `decode(..., timestamps="object")` |
| `array.array('f'/'d')`, 1-D float32/float64 `numpy` arrays → `array.array` | Packed float vectors, e.g. embeddings. Use `vectors="list"` or `"numpy"` to change the result type. |
| `btoon.ExtType(code, data)` | Any other extension type |

Other objects raise `TypeError` unless you pass `default=` (like `json.dumps`):

```python
btoon.encode(obj, default=lambda o: o.__dict__)
```

Self-referencing structures raise `btoon.BtoonError(code="max_depth")` instead of
crashing.

## Options

```python
btoon.encode(obj, *, compress=False, compression="zlib", auto_tabular=True,
             max_depth=128, hmac_key=None, default=None)
btoon.decode(data, *, strict=True, max_depth=128, max_decompressed_size=256 * 2**20,
             auto_decompress=True, hmac_key=None, timestamps="datetime", vectors="array")
btoon.compression_available("zstd")   # zlib is always available
```

## Untrusted input

`decode` is safe on hostile bytes. It is bounds-checked and depth-limited, it never
allocates more than the input justifies, it caps decompression, and in strict mode it
rejects invalid UTF-8, duplicate keys and trailing data. Every failure raises
`btoon.BtoonError` (a `ValueError`) whose `.code` is one of the spec's error codes:

```python
try:
    btoon.decode(payload, max_depth=32, max_decompressed_size=4 << 20)
except btoon.BtoonError as e:
    print(e.code, e)    # e.g. "truncated  unexpected end of input (at byte 7)"
```

Parsing during `decode`, and compression and signing during `encode`, run with the GIL
released, so other threads keep running. Converting to and from Python objects holds
the GIL.

## Interoperability

Output with `auto_tabular=False` is canonical MessagePack. Any MessagePack library can
read it, and `btoon.decode` reads any MessagePack whose map keys are strings, including
MessagePack timestamps. With tabular encoding on, generic MessagePack decoders see each
table as an opaque extension (type -10). The same bytes decode identically in the
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
