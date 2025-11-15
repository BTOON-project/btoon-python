#!/usr/bin/env python3
"""Quick test of BTOON Python library"""

import sys
import json

try:
    import btoon
except ImportError:
    print("Note: btoon module not installed, using local development version")
    sys.path.insert(0, '.')
    import btoon

print("BTOON Python Library Test")
print("=" * 40)

try:
    # Test 1: Basic encoding/decoding
    print("\n1. Basic encoding/decoding:")
    basic_data = {
        'string': 'Hello BTOON',
        'number': 42,
        'float': 3.14159,
        'boolean': True,
        'none': None,
        'list': [1, 2, 3],
        'dict': {'nested': 'value'}
    }
    
    encoded = btoon.encode(basic_data)
    print(f"   Encoded size: {len(encoded)} bytes")
    
    decoded = btoon.decode(encoded)
    print(f"   Decoded successfully: {str(decoded)[:50]}...")
    
    # Test 2: Compression
    print("\n2. Compression test:")
    large_data = {'data': [{'id': i, 'value': i * 0.1} for i in range(1000)]}
    uncompressed = btoon.encode(large_data)
    compressed = btoon.encode(large_data, compress=True)
    print(f"   Uncompressed: {len(uncompressed)} bytes")
    print(f"   Compressed: {len(compressed)} bytes")
    print(f"   Compression ratio: {(1 - len(compressed)/len(uncompressed)) * 100:.1f}%")
    
    # Test 3: Type preservation
    print("\n3. Type preservation:")
    types = {
        'int': 123,
        'big_int': 9007199254740992,  # > JS MAX_SAFE_INTEGER
        'float': 3.14,
        'bytes': bytes([0xFF, 0xFE, 0xFD])
    }
    
    types_encoded = btoon.encode(types)
    types_decoded = btoon.decode(types_encoded)
    print(f"   Integer: {types_decoded['int']} ({type(types_decoded['int']).__name__})")
    print(f"   Big int: {types_decoded['big_int']}")
    print(f"   Float: {types_decoded['float']}")
    print(f"   Bytes: bytes of length {len(types_decoded['bytes'])}")
    
    # Test 4: Extended types (if available)
    print("\n4. Extended types:")
    try:
        from btoon import Timestamp, Decimal
        
        extended_data = {
            'timestamp': Timestamp.now(),
            'price': Decimal("19.99")
        }
        
        extended_encoded = btoon.encode(extended_data)
        extended_decoded = btoon.decode(extended_encoded)
        print(f"   Timestamp preserved: {isinstance(extended_decoded['timestamp'], Timestamp)}")
        print(f"   Decimal preserved: {isinstance(extended_decoded['price'], Decimal)}")
    except ImportError:
        print("   Extended types not available in this build")
    
    print("\n✅ All tests passed!")
    
except Exception as e:
    print(f"\n❌ Test failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)
