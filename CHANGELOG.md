# Changelog

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
