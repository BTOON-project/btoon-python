"""
BTOON - Binary Tree Object Notation
High-performance binary serialization format for Python
"""

__version__ = "0.0.1"

import sys
import os

# Try to import the C++ extension module
try:
    # First try to import from the built extension
    from . import _btoon_native
    
    # Export native functions
    encode_native = _btoon_native.encode
    decode_native = _btoon_native.decode
    
except ImportError:
    # Fall back to pure Python implementation if available
    _btoon_native = None
    encode_native = None
    decode_native = None

# Import enhanced Python features
from .enhanced import (
    Timestamp,
    Decimal,
    Currency,
    Percentage,
    EnhancedEncoder,
    EnhancedDecoder,
    AsyncStreamEncoder,
    AsyncStreamDecoder,
    async_stream,
    open_btoon,
    from_dataframe,
    to_dataframe,
    from_numpy,
    to_numpy
)

# Main encode/decode functions
def encode(data, compress=False, auto_tabular=True, use_enhanced=True):
    """
    Encode Python data to BTOON format.
    
    Args:
        data: Python object to encode
        compress: Whether to compress the output
        auto_tabular: Automatically detect and use tabular encoding
        use_enhanced: Use enhanced encoder for special types
    
    Returns:
        bytes: BTOON encoded data
    """
    if use_enhanced:
        encoder = EnhancedEncoder()
        return encoder.encode(data, compress=compress, auto_tabular=auto_tabular)
    elif encode_native:
        return encode_native(data, compress=compress, auto_tabular=auto_tabular)
    else:
        raise ImportError("BTOON native module not available")

def decode(data, decompress=False, use_enhanced=True):
    """
    Decode BTOON data to Python objects.
    
    Args:
        data: BTOON encoded bytes
        decompress: Whether to decompress the input
        use_enhanced: Use enhanced decoder for special types
    
    Returns:
        Python object
    """
    if use_enhanced:
        decoder = EnhancedDecoder()
        return decoder.decode(data, decompress=decompress)
    elif decode_native:
        return decode_native(data, decompress=decompress)
    else:
        raise ImportError("BTOON native module not available")

# Export main API
__all__ = [
    # Core functions
    'encode',
    'decode',
    
    # Enhanced types
    'Timestamp',
    'Decimal',
    'Currency',
    'Percentage',
    
    # Enhanced encoders/decoders
    'EnhancedEncoder',
    'EnhancedDecoder',
    
    # Async support
    'AsyncStreamEncoder',
    'AsyncStreamDecoder',
    'async_stream',
    
    # File operations
    'open_btoon',
    
    # Data integration
    'from_dataframe',
    'to_dataframe',
    'from_numpy',
    'to_numpy',
]
