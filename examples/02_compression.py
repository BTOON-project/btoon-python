"""Compression envelopes, signing and safe decoding of untrusted input."""

import btoon

rows = [{"event": "click", "user": f"u{i % 50}", "ms": 1700000000000 + i} for i in range(5000)]

plain = btoon.encode(rows)
packed = btoon.encode(rows, compress=True)                       # zlib by default
print(f"plain {len(plain)} B -> zlib {len(packed)} B")
if btoon.compression_available("zstd"):
    print(f"zstd {len(btoon.encode(rows, compress=True, compression='zstd'))} B")

key = b"use 32+ random bytes as the key!"
sealed = btoon.encode(rows, compress=True, hmac_key=key)
assert btoon.decode(sealed, hmac_key=key) == rows               # verified before parsing

tampered = bytearray(sealed)
tampered[-1] ^= 1
try:
    btoon.decode(bytes(tampered), hmac_key=key)
except btoon.BtoonError as e:
    print("rejected:", e.code)                                   # authentication_failed

try:
    btoon.decode(b"\x91" * 10_000 + b"\xc0")                     # absurdly deep nesting
except btoon.BtoonError as e:
    print("rejected:", e.code)                                   # max_depth
