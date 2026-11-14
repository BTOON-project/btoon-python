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

import operator
import sys
import warnings
from typing import Any, Callable, Literal

from btoon._types import BtoonError, BtoonTypeError, ExtType, Timestamp
from btoon import _btoon

__version__ = "0.2.1"
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

# Core's limits (spec §7 and §7.1).
_DEFAULT_MAX_DEPTH: int = _btoon.DEFAULT_MAX_DEPTH  # 128
_MAX_DEPTH_LIMIT: int = _btoon.MAX_DEPTH_LIMIT  # 1000
_DEFAULT_MAX_DECOMPRESSED: int = _btoon.DEFAULT_MAX_DECOMPRESSED_SIZE  # 64 MiB
_DEFAULT_MAX_VALUES: int = _btoon.DEFAULT_MAX_VALUES  # 2**20


def _limit(name: str, value: Any, maximum: int | None = None) -> int:
    """Checks a numeric option: a non-negative int, and at most ``maximum`` if given."""
    try:
        n = operator.index(value)
    except TypeError:
        raise BtoonTypeError("invalid_argument", f"{name} must be an int, not {type(value).__name__}") from None
    if n < 0:
        raise BtoonTypeError("invalid_argument", f"{name} must not be negative")
    if maximum is not None and n > maximum:
        raise BtoonTypeError("invalid_argument", f"{name} must be at most {maximum}")
    return min(n, sys.maxsize)  # the native side takes a size_t; anything bigger is no limit anyway


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
        max_depth: nesting limit, at most 1000; deeper (or self-referencing) values raise
            ``BtoonError`` with code ``max_depth``.
        hmac_key: sign the result with HMAC-SHA256 using this key (bytes, or a str as
            UTF-8). ``None`` means no signature; an empty key raises ``BtoonTypeError``.
        default: called for unsupported objects, like ``json.dumps(default=...)``.

    :class:`ExtType` values with a code BTOON defines itself (-1 to -6, and -10) raise
    ``BtoonError`` with code ``invalid_argument``: those types are written from their
    typed values (``datetime``, :class:`Timestamp`, ``int``, float vectors, lists of dicts).
    """
    return _btoon.encode(
        obj,
        compress=compress,
        compression=compression,
        auto_tabular=auto_tabular,
        max_depth=_limit("max_depth", max_depth, _MAX_DEPTH_LIMIT),
        hmac_key=hmac_key,
        default=default,
    )


def decode(
    data: bytes | bytearray | memoryview,
    *,
    strict: bool = True,
    max_depth: int = _DEFAULT_MAX_DEPTH,
    max_decompressed_size: int = _DEFAULT_MAX_DECOMPRESSED,
    max_values: int = _DEFAULT_MAX_VALUES,
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

    The limits bound the memory a hostile document can claim (spec §7.1). Exceeding
    ``max_decompressed_size`` or ``max_values`` raises ``BtoonError`` with code
    ``size_limit``; raise them to decode bigger documents.

    Args:
        strict: reject trailing bytes, invalid UTF-8 and duplicate keys (spec §7). Map
            keys and column names must be valid UTF-8 in either mode.
        max_depth: nesting limit, at most 1000.
        max_decompressed_size: refuse compression envelopes that expand beyond this many
            bytes (default 64 MiB). It also caps the column-name bytes a table copies into
            its rows (rows × the total length of its column names).
        max_values: refuse documents that decode to more values than this (default
            2**20 = 1,048,576). Every value counts once: the top-level value, each list
            item, dict value and extension value, and each table row and cell. Dict keys
            don't count.
        auto_decompress: if False, compressed input is rejected instead of decompressed.
        hmac_key: require and verify an HMAC-SHA256 signature made with this key. ``None``
            means no key; an empty key raises ``BtoonTypeError``.
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
        max_depth=_limit("max_depth", max_depth, _MAX_DEPTH_LIMIT),
        max_decompressed_size=_limit("max_decompressed_size", max_decompressed_size),
        max_values=_limit("max_values", max_values),
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
