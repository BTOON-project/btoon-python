import btoon

# Test encoding and decoding
data = {'test': 123}
encoded = btoon.encode(data)
decoded = btoon.decode(encoded)
print(f'Original: {data}')
print(f'Decoded: {decoded}')
