"""Types of the native module (src/btoon_python.cpp). Use the btoon package, not this."""

from typing import Any, Callable

CORE_VERSION: str
SPEC_VERSION: str
DEFAULT_MAX_DEPTH: int
MAX_DEPTH_LIMIT: int
DEFAULT_MAX_DECOMPRESSED_SIZE: int
DEFAULT_MAX_VALUES: int

def encode(
    obj: Any,
    compress: bool,
    compression: str,
    auto_tabular: bool,
    max_depth: int,
    hmac_key: bytes | str | None,
    default: Callable[[Any], Any] | None,
) -> bytes: ...
def decode(
    data: bytes | bytearray | memoryview,
    strict: bool,
    max_depth: int,
    max_decompressed_size: int,
    max_values: int,
    auto_decompress: bool,
    hmac_key: bytes | str | None,
    timestamps: str,
    vectors: str,
) -> Any: ...
def compression_available(name: str) -> bool: ...
