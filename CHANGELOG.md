# Changelog

## 0.2.1 (2026-11-14)

- **Wheels for CPython 3.15.** 0.2.0 shipped wheels for 3.10–3.14 only, so on 3.15
  `pip install btoon` built from the sdist, which fails unless a C++20 compiler is
  installed. The wheel build now covers every CPython the pinned cibuildwheel supports
  from `requires-python` (3.10) up, instead of a hand-kept list, and CI tests 3.15.
- Same code as 0.2.0, on btoon-core 0.2.0. `test_version_info` now checks `__version__`
  against the installed package's metadata instead of a literal.

## 0.2.0 (2026-11-14)

BTOON spec 0.2, on btoon-core 0.2.0: `btoon.SPEC_VERSION` is `"0.2"`. The wire format
is unchanged, but decoding has new limits and encoding new rules, so some input that 0.1
accepted is now refused. Each behavior change is marked.

- **New: `decode(max_values=...)`** bounds how many values a document decodes to (spec
  §7.1). The top-level value, every list item, dict value and extension value, and every
  table row and cell count once; dict keys don't. Values inside a compression envelope
  count. **Behavior change:** the default is 2**20 (1,048,576), and a document over it
  raises `BtoonError` with code `size_limit`. Pass a larger `max_values` for bigger
  documents, such as a table of 100,000 rows × 10 columns (1,100,001 values).
- **Behavior change:** `max_decompressed_size` now defaults to 64 MiB, down from 256 MiB.
  It also bounds tables: rows × the total bytes of the column names (which are copied
  into every row) over the limit is `size_limit`. With `max_values`, this bounds what a
  small input can make the decoder allocate: in 0.1, a 32 KB envelope could take a
  process past 2 GB.
- **Behavior change:** `max_depth` above 1000 raises `BtoonTypeError` (code
  `invalid_argument`) in `encode` and `decode`. A raised limit used to let deep input or
  a deep object overflow the native stack (about 21,000 levels crashed the interpreter).
  The default stays 128.
- **Behavior change:** encoding an `ExtType` with a code BTOON defines itself (-1 to -6,
  and -10) raises `BtoonError` with code `invalid_argument` (spec §5.7). It used to be
  written verbatim, so `ExtType(-4, b"\x05")` produced the non-canonical `d4fc05`, and
  `ExtType(-1, b"\x01\x02\x03")` a timestamp that every decoder rejects. Those types are
  written from `datetime`, `Timestamp`, `int`, float vectors and lists of dicts, so there
  is no longer a way to write the date (-2) and datetime (-3) extensions. `ExtType`
  still constructs with any code in -128..127, and the codes reserved for future versions
  (-7 to -9, -11 to -128) are written as given.
- **Behavior change:** with `strict=False`, map keys and table column names must still be
  valid UTF-8 (`invalid_utf8`). They used to decode with surrogate escapes. String values
  are unchanged.
- **Behavior change:** table column names are checked front to back, each for UTF-8 and
  then against the earlier names. A duplicate name ahead of an invalid one is now
  `invalid_tabular` instead of `invalid_utf8`.
- **Behavior change:** `max_depth`, `max_decompressed_size` and `max_values` are checked
  in Python: a negative or non-integer value raises `BtoonTypeError` (code
  `invalid_argument`) instead of pybind11's plain `TypeError`, and a value beyond the
  platform's `size_t` means no limit instead of raising.
- **Fixed:** two invalid arguments raised `RuntimeError` instead of `BtoonTypeError` /
  `BtoonError` (code `invalid_argument`): an `hmac_key` str with a lone surrogate, and an
  `ExtType` whose code is outside -128..127 because it was built with `ExtType._make()` or
  `tuple.__new__()`, which skip the constructor's check.
- Unchanged: `hmac_key=None` is the only way to say "no key", and an empty key still
  raises `BtoonTypeError` (spec §8 now requires this).
- **Tests:** the conformance harness honors every vector option (`strict`, `max_depth`,
  `max_decompressed_size`, `max_values`, `hmac_key`), runs the `encode_error` vectors,
  and compares float vector elements bit for bit, including NaN payloads. It reads
  `BTOON_VECTORS` first.

## 0.1.2 (2026-11-13)

- **Core:** built on btoon-core 0.1.5. Nested containers no longer reserve memory against
  the same input at every level. On the review's inputs, 1 MiB of nested headers reserved
  5 GB on 0.1.4; now it peaks near 100 MB.
- **Fixed (memory safety):**
  - Some `datetime` subclasses override `utcoffset()` to return something other than a
    `timedelta`, such as an int, float or tuple. The encoder read that object as a
    `timedelta`: a heap over-read that produced a garbage timestamp.
  - Now a non-timedelta raises `BtoonTypeError`, and an offset of a day or more raises
    `BtoonError` (`invalid_argument`). These are the rules CPython's `datetime` enforces.
  - The bound also removes a signed overflow with huge offsets.

## 0.1.1 (2026-11-13)

- **Core:** built on btoon-core 0.1.4 (was 0.1.0), so `btoon.CORE_VERSION` is `"0.1.4"`.
  The Python API and behavior are unchanged. Core's fixes since 0.1.0 are in parts this
  package doesn't expose: the C++ `Decoder` class's `hmac_key`, the zero-copy C API,
  shared memory and Android builds. The encoder and `decode()` paths the package uses
  behave the same.

## 0.1.0 (2026-11-07)

A rewrite on btoon-core 0.1.0 (BTOON spec 0.1).

- **Fixed:**
  - `compress=True` did nothing.
  - Compressed data didn't decode by default.
  - Dicts shaped like `{"type": …, "data": …}` were silently turned into extensions.
  - Timestamps lost their nanoseconds, and dates decoded as dicts.
  - Self-referencing objects crashed the interpreter.
  - Every core decoder bug from 0.0.x (heap over-read, stack overflow on deep nesting,
    unbounded allocations) is gone too.
- **New types:**
  - `int` of any size (bigint extension);
  - aware `datetime` in both directions, plus `btoon.Timestamp` with nanoseconds and an
    optional UTC offset;
  - `bytearray`, `memoryview` and `tuple`;
  - float vectors from `array.array` or numpy;
  - `btoon.ExtType`.
- **New options:**
  - `compression=` (zlib by default), `hmac_key=` (signed envelopes), `max_depth=`;
  - `default=` hook, `strict=`, `max_decompressed_size=`, `auto_decompress=`,
    `timestamps=`, `vectors=`;
  - `btoon.compression_available()`.
- **Errors:** they are now `btoon.BtoonError` (a `ValueError` subclass) with a
  spec-defined `.code`. Unsupported objects raise `btoon.BtoonTypeError`, which is also a
  `TypeError`.
- **Performance:**
  - Encoding goes straight from Python objects to bytes, with no intermediate tree.
    For tabular data that is about 2.3× faster than msgpack-python.
  - Decoding releases the GIL while parsing, and decoded tables share their key
    objects.
- **Build:** scikit-build-core + CMake, compiling core from the `core/` submodule; no
  prebuilt library needed. Wheels come from cibuildwheel for Linux (x86_64, aarch64,
  musl), macOS (arm64, x86_64) and Windows. Python 3.10+.
- **Deprecated:** `decode(decompress=...)`. Envelopes are detected automatically.

## 0.0.1

Initial release.
