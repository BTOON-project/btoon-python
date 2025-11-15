#!/usr/bin/env python3
"""
Basic BTOON encoding and decoding example
"""

import sys
import json
from pathlib import Path

# Add parent directory to path for development
sys.path.insert(0, str(Path(__file__).parent.parent))

import btoon

def main():
    print("BTOON Basic Example")
    print("=" * 40)
    
    # Simple data types
    simple_data = {
        'message': 'Hello, BTOON!',
        'count': 42,
        'pi': 3.14159,
        'active': True,
        'empty': None
    }
    
    print("\n1. Simple data encoding:")
    print(f"Original: {simple_data}")
    
    encoded = btoon.encode(simple_data)
    print(f"Encoded size: {len(encoded)} bytes")
    print(f"Encoded (hex): {encoded[:25].hex()}...")
    
    decoded = btoon.decode(encoded)
    print(f"Decoded: {decoded}")
    assert decoded == simple_data, "Decode mismatch!"
    
    # Nested structures
    nested_data = {
        'user': {
            'id': 1001,
            'name': 'Alice',
            'email': 'alice@example.com',
            'roles': ['admin', 'user'],
            'settings': {
                'theme': 'dark',
                'notifications': True,
                'language': 'en'
            }
        },
        'metadata': {
            'created': '2024-01-01T00:00:00Z',
            'version': '0.0.1'
        }
    }
    
    print("\n2. Nested structure encoding:")
    nested_encoded = btoon.encode(nested_data)
    json_size = len(json.dumps(nested_data))
    btoon_size = len(nested_encoded)
    
    print(f"Original size (JSON): {json_size} bytes")
    print(f"BTOON size: {btoon_size} bytes")
    print(f"Size reduction: {round((1 - btoon_size / json_size) * 100)}%")
    
    # Arrays of different types
    array_data = {
        'numbers': [1, 2, 3, 4, 5],
        'strings': ['apple', 'banana', 'cherry'],
        'mixed': [42, 'hello', True, None, {'key': 'value'}],
        'matrix': [[1, 2], [3, 4], [5, 6]]
    }
    
    print("\n3. Array encoding:")
    array_encoded = btoon.encode(array_data)
    array_decoded = btoon.decode(array_encoded)
    
    print(f"Numbers preserved: {array_decoded['numbers']}")
    print(f"Strings preserved: {array_decoded['strings']}")
    print(f"Mixed types preserved: {array_decoded['mixed']}")
    print(f"Matrix preserved: {array_decoded['matrix']}")
    
    # Binary data
    binary_data = {
        'id': 'file-001',
        'content': b'Binary content here',
        'checksum': bytes([0xDE, 0xAD, 0xBE, 0xEF])
    }
    
    print("\n4. Binary data encoding:")
    binary_encoded = btoon.encode(binary_data)
    binary_decoded = btoon.decode(binary_encoded)
    
    print(f"Content preserved: {binary_decoded['content'].decode('utf-8')}")
    print(f"Checksum preserved: {' '.join(f'0x{b:02X}' for b in binary_decoded['checksum'])}")
    
    # Large integers (beyond JavaScript's safe integer range)
    large_data = {
        'small_int': 42,
        'large_int': 9007199254740993,  # > JS MAX_SAFE_INTEGER
        'huge_int': 18446744073709551615,  # max uint64
        'negative': -9223372036854775808  # min int64
    }
    
    print("\n5. Large integer handling:")
    large_encoded = btoon.encode(large_data)
    large_decoded = btoon.decode(large_encoded)
    
    for key, value in large_decoded.items():
        print(f"  {key}: {value} ({type(value).__name__})")
        assert value == large_data[key], f"Integer {key} not preserved!"
    
    # Unicode and special characters
    unicode_data = {
        'english': 'Hello World',
        'chinese': '你好世界',
        'japanese': 'こんにちは世界',
        'arabic': 'مرحبا بالعالم',
        'emoji': '🚀 🌟 🎉 🔥',
        'special': 'café, naïve, résumé'
    }
    
    print("\n6. Unicode handling:")
    unicode_encoded = btoon.encode(unicode_data)
    unicode_decoded = btoon.decode(unicode_encoded)
    
    for key, value in unicode_decoded.items():
        assert value == unicode_data[key], f"Unicode {key} not preserved!"
        print(f"  {key}: {value} ✓")
    
    print("\n✅ All basic examples completed successfully!")

if __name__ == '__main__':
    main()
