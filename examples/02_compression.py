#!/usr/bin/env python3
"""
BTOON compression examples
"""

import sys
import random
import time
from pathlib import Path

# Add parent directory to path for development
sys.path.insert(0, str(Path(__file__).parent.parent))

import btoon

def generate_sample_data(count):
    """Generate sample data with repetition (good for compression)"""
    templates = ['Alice', 'Bob', 'Charlie', 'David', 'Eve']
    departments = ['Engineering', 'Sales', 'Marketing', 'HR', 'Finance']
    
    data = []
    for i in range(count):
        data.append({
            'id': i,
            'name': templates[i % len(templates)],
            'email': f'user{i}@example.com',
            'department': departments[i % len(departments)],
            'score': random.randint(0, 100),
            'active': i % 2 == 0,
            'tags': ['user', 'employee', 'active'],
            'metadata': {
                'created': '2024-01-01T00:00:00Z',
                'updated': '2024-01-01T00:00:00Z',
                'version': 1
            }
        })
    return data

def main():
    print("BTOON Compression Example")
    print("=" * 40)
    
    # Test different compression scenarios
    print("\n1. Small data compression:")
    small_data = generate_sample_data(10)
    small_uncompressed = btoon.encode(small_data)
    small_compressed = btoon.encode(small_data, compress=True)
    
    print(f"Uncompressed: {len(small_uncompressed)} bytes")
    print(f"Compressed: {len(small_compressed)} bytes")
    if len(small_compressed) < len(small_uncompressed):
        ratio = (1 - len(small_compressed) / len(small_uncompressed)) * 100
        print(f"Ratio: {ratio:.1f}% reduction")
    else:
        print("Note: Small data may not compress well or may even increase in size")
    
    print("\n2. Medium data compression:")
    medium_data = generate_sample_data(100)
    medium_uncompressed = btoon.encode(medium_data)
    medium_compressed = btoon.encode(medium_data, compress=True)
    
    print(f"Uncompressed: {len(medium_uncompressed)} bytes")
    print(f"Compressed: {len(medium_compressed)} bytes")
    print(f"Ratio: {(1 - len(medium_compressed) / len(medium_uncompressed)) * 100:.1f}% reduction")
    
    print("\n3. Large data compression:")
    large_data = generate_sample_data(1000)
    large_uncompressed = btoon.encode(large_data)
    large_compressed = btoon.encode(large_data, compress=True)
    
    print(f"Uncompressed: {len(large_uncompressed)} bytes")
    print(f"Compressed: {len(large_compressed)} bytes")
    print(f"Ratio: {(1 - len(large_compressed) / len(large_uncompressed)) * 100:.1f}% reduction")
    
    # Test compression with different data types
    print("\n4. Compression by data type:")
    
    # Highly repetitive strings
    repetitive_data = {
        'logs': [{'timestamp': i, 'message': 'ERROR: Connection timeout', 'level': 'ERROR'} 
                 for i in range(100)]
    }
    rep_uncomp = btoon.encode(repetitive_data)
    rep_comp = btoon.encode(repetitive_data, compress=True)
    print(f"Repetitive strings - Uncompressed: {len(rep_uncomp)}, Compressed: {len(rep_comp)}, "
          f"Ratio: {(1 - len(rep_comp) / len(rep_uncomp)) * 100:.1f}%")
    
    # Random numbers (poor compression)
    random_data = {
        'values': [random.random() for _ in range(1000)]
    }
    rand_uncomp = btoon.encode(random_data)
    rand_comp = btoon.encode(random_data, compress=True)
    print(f"Random numbers - Uncompressed: {len(rand_uncomp)}, Compressed: {len(rand_comp)}, "
          f"Ratio: {(1 - len(rand_comp) / len(rand_uncomp)) * 100:.1f}%")
    
    # Sequential data (good compression)
    sequential_data = {
        'sequence': list(range(1000)),
        'constant': [42] * 1000
    }
    seq_uncomp = btoon.encode(sequential_data)
    seq_comp = btoon.encode(sequential_data, compress=True)
    print(f"Sequential data - Uncompressed: {len(seq_uncomp)}, Compressed: {len(seq_comp)}, "
          f"Ratio: {(1 - len(seq_comp) / len(seq_uncomp)) * 100:.1f}%")
    
    # Test decompression
    print("\n5. Decompression test:")
    test_data = {'test': 'compression', 'array': [1, 2, 3, 4, 5], 'nested': {'key': 'value'}}
    compressed = btoon.encode(test_data, compress=True)
    decompressed = btoon.decode(compressed, decompress=True)
    print(f"Original: {test_data}")
    print(f"After compress/decompress: {decompressed}")
    print(f"Data integrity: {'✅ OK' if test_data == decompressed else '❌ FAILED'}")
    
    # Performance comparison
    print("\n6. Performance comparison (1000 records):")
    perf_data = generate_sample_data(1000)
    
    # Uncompressed
    start = time.perf_counter()
    for _ in range(10):
        encoded = btoon.encode(perf_data)
    uncomp_time = (time.perf_counter() - start) / 10
    
    # Compressed
    start = time.perf_counter()
    for _ in range(10):
        encoded = btoon.encode(perf_data, compress=True)
    comp_time = (time.perf_counter() - start) / 10
    
    print(f"Uncompressed encoding: {uncomp_time * 1000:.2f} ms")
    print(f"Compressed encoding: {comp_time * 1000:.2f} ms")
    print(f"Compression overhead: {(comp_time - uncomp_time) * 1000:.2f} ms")
    
    # Test different compression algorithms if available
    print("\n7. Different compression algorithms (if available):")
    algorithms = ['zlib', 'lz4', 'zstd', 'brotli', 'snappy']
    test_data = generate_sample_data(500)
    
    for algo in algorithms:
        try:
            # Note: This assumes the enhanced encoder supports algorithm selection
            # The actual API might differ
            encoded = btoon.encode(test_data, compress=True)
            print(f"  {algo}: {len(encoded)} bytes")
        except:
            # Algorithm might not be available
            pass
    
    print("\n✅ All compression examples completed successfully!")

if __name__ == '__main__':
    main()
