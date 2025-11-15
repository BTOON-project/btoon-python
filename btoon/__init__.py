"""
BTOON - Binary Tree Object Notation
High-performance binary serialization format for Python
"""

__version__ = "0.0.1"

# Import the native C++ extension module
try:
    from btoon._btoon import encode as _encode, decode as _decode, version
except ImportError as e:
    raise ImportError(
        "Failed to import BTOON native module. "
        "Please ensure the package is properly installed with: pip install -e .\n"
        f"Error: {e}"
    )

# Main encode/decode functions that wrap the native implementation
def encode(data, compress=False, auto_tabular=True):
    """
    Encode Python data to BTOON format.

    Args:
        data: Python object to encode (dict, list, str, int, float, bool, None, bytes)
        compress: Whether to compress the output (default: False)
        auto_tabular: Automatically detect and use tabular encoding for arrays of uniform objects (default: True)

    Returns:
        bytes: BTOON encoded data

    Examples:
        >>> import btoon
        >>> data = {"name": "Alice", "age": 30}
        >>> encoded = btoon.encode(data)
        >>> decoded = btoon.decode(encoded)
        >>> assert data == decoded
    """
    return _encode(data, compress=compress, auto_tabular=auto_tabular)


def decode(data, decompress=False):
    """
    Decode BTOON data to Python objects.

    Args:
        data: BTOON encoded bytes
        decompress: Whether to decompress the input (default: False)

    Returns:
        Python object (dict, list, str, int, float, bool, None, or bytes)

    Examples:
        >>> import btoon
        >>> data = {"items": [1, 2, 3]}
        >>> encoded = btoon.encode(data)
        >>> decoded = btoon.decode(encoded)
        >>> assert data == decoded
    """
    return _decode(data, decompress=decompress)


# Export main API
__all__ = [
    'encode',
    'decode',
    'version',
    '__version__',
]
