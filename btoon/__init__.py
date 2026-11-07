"""BTOON — compact binary serialization for Python.

BTOON is MessagePack plus a columnar encoding for lists of uniform dicts, extra
types (timestamps, big integers, float vectors) and optional compression and
HMAC-signed envelopes. This package wraps the C++ reference implementation.

    >>> import btoon
    >>> data = {"users": [{"id": i, "name": f"user{i}"} for i in range(100)]}
    >>> btoon.decode(btoon.encode(data)) == data
    True
"""

from __future__ import annotations

import warnings
from typing import Any, Callable, Literal

from btoon._types import BtoonError, BtoonTypeError, ExtType, Timestamp
from btoon import _btoon

__version__ = "0.1.0"
SPEC_VERSION: str = _btoon.SPEC_VERSION
CORE_VERSION: str = _btoon.CORE_VERSION

__all__ = [
    "encode",
    "decode",
    "compression_available",
    "BtoonError",
    "BtoonTypeError",
    "ExtType",
    "Timestamp",
    "SPEC_VERSION",
    "__version__",
]

Compression = Literal["zlib", "lz4", "zstd", "brotli", "snappy", "auto"]

_DEFAULT_MAX_DEPTH = 128
_DEFAULT_MAX_DECOMPRESSED = 256 * 1024 * 1024


def encode(
    obj: Any,
    *,
    compress: bool = False,
    compression: Compression = "zlib",
    auto_tabular: bool = True,
    max_depth: int = _DEFAULT_MAX_DEPTH,
    hmac_key: bytes | str | None = None,
    default: Callable[[Any], Any] | None = None,
) -> bytes:
    """Encodes ``obj`` to canonical BTOON bytes.

    Supported types: ``None``, ``bool``, ``int`` (any size), ``float``, ``str``,
    ``bytes``/``bytearray``/``memoryview``, ``list``/``tuple``, ``dict`` with ``str``
    keys, ``datetime`` (naive values are taken as UTC), :class:`Timestamp`,
    :class:`ExtType`, ``array.array('f'|'d')`` and 1-D float32/float64 numpy arrays
    (as float vectors). Anything else is passed to ``default`` if given, whose return
    value is encoded instead; otherwise :class:`BtoonTypeError` is raised.

    Args:
        compress: wrap the result in a compression envelope (skipped when it wouldn't
            save at least 5%, or for inputs under 256 bytes).
        compression: algorithm for ``compress``; see :func:`compression_available`.
        auto_tabular: use the columnar encoding for lists of uniform dicts when smaller.
        max_depth: nesting limit; deeper (or self-referencing) values raise ``BtoonError``.
        hmac_key: sign the result with HMAC-SHA256 using this key.
        default: called for unsupported objects, like ``json.dumps(default=...)``.
    """
    return _btoon.encode(
        obj,
        compress=compress,
        compression=compression,
        auto_tabular=auto_tabular,
        max_depth=max_depth,
        hmac_key=hmac_key,
        default=default,
    )


def decode(
    data: bytes | bytearray | memoryview,
    *,
    strict: bool = True,
    max_depth: int = _DEFAULT_MAX_DEPTH,
    max_decompressed_size: int = _DEFAULT_MAX_DECOMPRESSED,
    auto_decompress: bool = True,
    hmac_key: bytes | str | None = None,
    timestamps: Literal["datetime", "object"] = "datetime",
    vectors: Literal["array", "list", "numpy"] = "array",
    decompress: bool | None = None,
) -> Any:
    """Decodes BTOON bytes. Safe on untrusted input: malformed data raises :class:`BtoonError`.

    Compression envelopes are detected and decompressed automatically (bounded by
    ``max_decompressed_size``). Integers decode to ``int``, binary to ``bytes``, maps to
    ``dict``, timestamps/dates to aware ``datetime`` (UTC, or the stored offset; values
    outside ``datetime``'s range decode as :class:`Timestamp`), float vectors to
    ``array.array`` (or lists / numpy arrays via ``vectors``), unknown extensions to
    :class:`ExtType`.

    Args:
        strict: reject trailing bytes, invalid UTF-8 and duplicate keys (spec §7).
        max_depth: nesting limit.
        max_decompressed_size: refuse compression envelopes that expand beyond this.
        auto_decompress: if False, compressed input is rejected instead of decompressed.
        hmac_key: require and verify an HMAC-SHA256 signature made with this key.
        timestamps: ``"datetime"`` (microsecond precision) or ``"object"`` (:class:`Timestamp`).
        vectors: how float vectors are returned.
        decompress: deprecated alias of ``auto_decompress`` (0.0.x API).
    """
    if decompress is not None:
        warnings.warn(
            "decode(decompress=...) is deprecated; compressed input is detected automatically "
            "(use auto_decompress=False to reject it)",
            DeprecationWarning,
            stacklevel=2,
        )
        auto_decompress = decompress or auto_decompress
    return _btoon.decode(
        data,
        strict=strict,
        max_depth=max_depth,
        max_decompressed_size=max_decompressed_size,
        auto_decompress=auto_decompress,
        hmac_key=hmac_key,
        timestamps=timestamps,
        vectors=vectors,
    )


def compression_available(algorithm: Compression) -> bool:
    """Returns True if this build supports ``algorithm`` (``"zlib"`` always is)."""
    return _btoon.compression_available(algorithm)


def version() -> str:
    """Returns the package version (kept for compatibility with 0.0.x)."""
    return __version__
