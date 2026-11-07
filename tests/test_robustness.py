"""Hostile input through the Python layer, and reference-count / leak sanity checks."""

import gc
import json
import os
import random
import sys
from pathlib import Path

import pytest

import btoon


def _load_vectors():
    root = Path(__file__).resolve().parent.parent
    for c in (os.environ.get("BTOON_VECTORS"), root / "core/tests/vectors/btoon-vectors.json",
              root.parent / "btoon-core/tests/vectors/btoon-vectors.json"):
        if c and Path(c).is_file():
            return json.loads(Path(c).read_text(encoding="utf-8"))
    raise FileNotFoundError("btoon-vectors.json not found")


VECTORS = _load_vectors()


def test_mutation_fuzz_only_raises_btoon_errors():
    seeds = [bytes.fromhex(v["hex"]) for v in VECTORS["vectors"] if 0 < len(v["hex"]) <= 8192]
    rng = random.Random(0xB7001)
    decoded = 0
    for _ in range(30000):
        buf = bytearray(rng.choice(seeds))
        for _ in range(rng.randint(1, 4)):
            op = rng.randrange(4)
            if op == 0 and buf:
                buf[rng.randrange(len(buf))] ^= 1 << rng.randrange(8)
            elif op == 1 and buf:
                buf[rng.randrange(len(buf))] = rng.randrange(256)
            elif op == 2:
                del buf[rng.randrange(len(buf) + 1):]
            else:
                buf.insert(rng.randrange(len(buf) + 1), rng.randrange(256))
        for strict in (True, False):
            try:
                value = btoon.decode(bytes(buf), strict=strict, timestamps="object")
            except btoon.BtoonError:
                continue
            decoded += 1
            if strict:  # whatever decodes strictly must re-encode and decode to the same thing
                again = btoon.decode(btoon.encode(value), timestamps="object")
                assert btoon.encode(again) == btoon.encode(value)
    assert decoded > 1000


def _rss_kb():
    import resource

    rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return rss / 1024 if sys.platform == "darwin" else rss


@pytest.mark.skipif(sys.platform == "win32", reason="resource module unavailable")
def test_no_leaks_in_hot_loops():
    doc = {"rows": [{"id": i, "s": "x" * 20, "b": b"\x00" * 10, "f": i / 3, "big": 2**70 + i} for i in range(200)]}
    data = btoon.encode(doc)
    for _ in range(300):  # warm up allocator pools
        btoon.decode(btoon.encode(doc))
    gc.collect()
    before = _rss_kb()
    for _ in range(3000):
        btoon.decode(btoon.encode(doc))
        try:
            btoon.decode(data[:-3])
        except btoon.BtoonError:
            pass
    gc.collect()
    grown_mb = (_rss_kb() - before) / 1024
    assert grown_mb < 20, f"RSS grew by {grown_mb:.1f} MB"


def test_refcounts_of_shared_objects_are_stable():
    s = "shared-string-object"
    payload = [s] * 100
    before = sys.getrefcount(s)
    for _ in range(1000):
        btoon.encode(payload)
    assert sys.getrefcount(s) == before
    key = "a-key-that-is-not-interned-%d" % 12345  # short/literal strings may be immortal (PEP 683)
    rows = btoon.decode(btoon.encode([{key: i} for i in range(50)]))
    k = next(iter(rows[0]))
    assert all(next(iter(r)) is k for r in rows)  # decoded rows share one key object
    assert sys.getrefcount(k) <= 50 + 3  # one per row + getrefcount's argument + slack
    del rows
    gc.collect()
    assert sys.getrefcount(k) <= 3  # the decoder kept no hidden references
