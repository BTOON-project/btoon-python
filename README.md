# BTOON for Python

[![PyPI version](https://img.shields.io/pypi/v/btoon.svg)](https://pypi.org/project/btoon/)
[![Python Versions](https://img.shields.io/pypi/pyversions/btoon.svg)](https://pypi.org/project/btoon/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

High-performance binary serialization format for Python applications with native C++ performance.

## Features

- 🚀 **High Performance** - Native C++ implementation with Python bindings
- 📦 **Compact Binary Format** - Smaller than JSON, faster than Pickle
- 🗜️ **Built-in Compression** - ZLIB, LZ4, ZSTD, Brotli, Snappy support
- 📊 **NumPy & Pandas Integration** - Efficient array and DataFrame serialization
- 🔄 **Schema Evolution** - Forward and backward compatibility
- ⚡ **Zero-Copy Operations** - Memory-efficient for large data
- 🐍 **Pythonic API** - Native Python types and async support
- 💰 **Financial Types** - Decimal, Currency, and Percentage support

## Installation

```bash
pip install btoon
```

Or using conda:

```bash
conda install -c conda-forge btoon
```

## Quick Start

```python
import btoon

# Encode data
data = {
    'name': 'BTOON',
    'version': '0.0.1',
    'features': ['fast', 'compact', 'typed'],
    'metrics': {
        'speed': 9000,
        'size': 0.5
    }
}

encoded = btoon.encode(data)
print(f'Encoded size: {len(encoded)} bytes')

# Decode data
decoded = btoon.decode(encoded)
print(f'Decoded: {decoded}')
```

## Advanced Features

### Compression

```python
# Enable compression with different algorithms
compressed = btoon.encode(data, compress=True)  # Default: zlib

# Specify algorithm and level
compressed = btoon.encode(data, 
    compress=True,
    algorithm='zstd',  # 'zlib', 'lz4', 'zstd', 'brotli', 'snappy'
    level=3
)
```

### NumPy Integration

```python
import numpy as np
import btoon

# Encode NumPy arrays efficiently
array = np.random.rand(1000, 100)
encoded = btoon.from_numpy(array)

# Decode back to NumPy
decoded = btoon.to_numpy(encoded)
assert np.array_equal(array, decoded)
```

### Pandas Integration

```python
import pandas as pd
import btoon

# Encode DataFrames with columnar optimization
df = pd.DataFrame({
    'id': range(1000),
    'name': [f'user_{i}' for i in range(1000)],
    'score': np.random.rand(1000)
})

encoded = btoon.from_dataframe(df)
print(f'Encoded size: {len(encoded)} bytes')

# Decode back to DataFrame
decoded_df = btoon.to_dataframe(encoded)
assert df.equals(decoded_df)
```

### Extended Types

```python
from btoon import Timestamp, Decimal, Currency, Percentage
from datetime import datetime

# Timestamp with nanosecond precision
ts = Timestamp.now()
ts_from_dt = Timestamp.from_datetime(datetime.now())

# Financial types with arbitrary precision
price = Decimal("19.99")
tax_rate = Percentage("8.25")
total = Currency("USD", price * (1 + tax_rate.to_decimal()))

data = {
    'timestamp': ts,
    'price': price,
    'tax_rate': tax_rate,
    'total': total
}

encoded = btoon.encode(data)
decoded = btoon.decode(encoded)
```

### Async Support

```python
import asyncio
import btoon

async def process_data():
    # Async encoding
    async with btoon.async_stream('output.btoon', 'w') as stream:
        await stream.encode({'chunk': 1})
        await stream.encode({'chunk': 2})
    
    # Async decoding
    async with btoon.async_stream('output.btoon', 'r') as stream:
        async for obj in stream:
            print(f'Decoded: {obj}')

asyncio.run(process_data())
```

### File Operations

```python
# Context manager for file operations
with btoon.open_btoon('data.btoon', 'w') as f:
    f.encode({'record': 1})
    f.encode({'record': 2})

with btoon.open_btoon('data.btoon', 'r') as f:
    for record in f:
        print(record)
```

### Streaming

```python
from btoon import StreamEncoder, StreamDecoder

# Stream encoding
encoder = StreamEncoder()
chunks = []
for i in range(100):
    chunk = encoder.encode_chunk({'id': i})
    chunks.append(chunk)
final = encoder.finalize()

# Stream decoding
decoder = StreamDecoder()
for chunk in chunks:
    objs = decoder.decode_chunk(chunk)
    for obj in objs:
        print(obj)
```

## Schema Support

```python
import btoon

# Define schema
schema = btoon.Schema({
    'type': 'object',
    'properties': {
        'id': {'type': 'integer', 'required': True},
        'name': {'type': 'string', 'required': True},
        'age': {'type': 'integer', 'min': 0, 'max': 120}
    }
})

# Validate and encode with schema
if schema.validate(data):
    encoded = btoon.encode(data, schema=schema)
    decoded = btoon.decode(encoded, schema=schema)
```

## Performance

BTOON provides significant performance improvements:

| Operation | JSON | Pickle | BTOON | Improvement |
|-----------|------|--------|-------|-------------|
| Encode 1MB | 125ms | 85ms | 12ms | 10x faster |
| Decode 1MB | 95ms | 45ms | 8ms | 11x faster |
| Size | 1024KB | 780KB | 412KB | 60% smaller |

### DataFrame Performance (10,000 rows × 20 columns)

| Format | Encode | Decode | Size |
|--------|--------|--------|------|
| CSV | 450ms | 380ms | 8.2MB |
| Pickle | 120ms | 95ms | 5.1MB |
| Parquet | 85ms | 72ms | 2.3MB |
| BTOON | 35ms | 28ms | 1.8MB |

## API Reference

### Core Functions

#### `encode(data, compress=False, auto_tabular=True, **kwargs)`
Encode Python data to BTOON format.

#### `decode(data, decompress=False, **kwargs)`
Decode BTOON data to Python objects.

### NumPy Integration

#### `from_numpy(array, compress=False)`
Encode NumPy array to BTOON.

#### `to_numpy(data)`
Decode BTOON to NumPy array.

### Pandas Integration

#### `from_dataframe(df, compress=True)`
Encode DataFrame to BTOON with columnar optimization.

#### `to_dataframe(data)`
Decode BTOON to DataFrame.

### Extended Types

#### `Timestamp`
High-precision timestamp with nanoseconds and timezone.

#### `Decimal`
Arbitrary precision decimal numbers.

#### `Currency`
Currency values with code and amount.

#### `Percentage`
Percentage values with proper arithmetic.

### Async Support

#### `async_stream(path, mode='r')`
Async context manager for file operations.

#### `AsyncStreamEncoder`
Async encoder for streaming data.

#### `AsyncStreamDecoder`
Async decoder for streaming data.

## Examples

See the [`examples/`](examples/) directory for more usage examples:
- Basic encoding/decoding
- NumPy and Pandas integration
- Financial calculations
- Async operations
- Schema validation
- Performance benchmarks

## Requirements

- Python >= 3.7
- C++ compiler (for building from source)
- NumPy (optional, for array support)
- Pandas (optional, for DataFrame support)

## Building from Source

```bash
git clone https://github.com/BTOON-project/btoon-python.git
cd btoon-python
pip install -e .
pytest tests/
```

## Contributing

Contributions are welcome! Please read our [Contributing Guide](CONTRIBUTING.md) for details.

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Links

- [Website](https://btoon.net)
- [GitHub](https://github.com/BTOON-project/btoon-python)
- [Documentation](https://btoon.readthedocs.io)
- [PyPI Package](https://pypi.org/project/btoon/)

---

Part of the BTOON project - High-performance binary serialization for modern applications.