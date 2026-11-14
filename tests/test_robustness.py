"""Hostile input through the Python layer, and reference-count / leak sanity checks."""

import gc
import json
import os
import random
import struct
import subprocess
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
    # encode_error vectors (spec §10) have a value but no bytes.
    seeds = [bytes.fromhex(v["hex"]) for v in VECTORS["vectors"] if 0 < len(v.get("hex", "")) <= 8192]
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


# --- review 2026-11-13: crashes and leaks at the edges ---------------------------------------

def _run(code, *args, env=None):
    return subprocess.run([sys.executable, "-I", "-c", code, *map(str, args)], capture_output=True, text=True,
                          timeout=600, env=env)


_ALLOCATION_FAILURES = r'''
import datetime as dt, sys, _testcapi, btoon
doc = {"a": [1, 2**70, 1.5, "s" * 70, b"b", None, True, {"x": [3]}], "k" * 100: btoon.Timestamp(5, 6, 60),
       "rows": [{"id": i, "v": i * 2} for i in range(5)], "d": btoon.Date(5), "n": btoon.DateTime(-5),
       "t": dt.datetime(2024, 1, 1, tzinfo=dt.timezone.utc)}
data = btoon.encode(doc)
calls = {
    "decode": lambda: btoon.decode(data),
    "decode_object": lambda: btoon.decode(data, timestamps="object", vectors="list"),
    "decode_int": lambda: btoon.decode(b"\x01"),
    "encode": lambda: btoon.encode(doc),
}
for name, call in calls.items():
    for start in range(1, int(sys.argv[1])):
        print(name, start, flush=True)  # the last line names the failing call if this crashes
        _testcapi.set_nomemory(start, start + 1)
        try:
            call()
        except MemoryError:
            pass
        finally:
            _testcapi.remove_mem_hooks()
print("done")
'''


def test_allocation_failures_raise_memory_error():
    # Review 2026-11-13: an allocation failure inside pybind11's keyword matching (every
    # call passed keywords) or an unchecked PyLong_FromLongLong segfaulted; others raised
    # RuntimeError. Each allocation in turn fails here; every call must succeed or raise
    # MemoryError.
    pytest.importorskip("_testcapi")
    out = _run(_ALLOCATION_FAILURES, 120)
    assert out.returncode == 0 and out.stdout.endswith("done\n"), (
        f"exit {out.returncode} after {out.stdout.splitlines()[-1:]}: {out.stderr[-2000:]}")


_COPY_FAILS = r'''
import resource, btoon
ba = bytearray(b"\x90") + bytearray(256 * 1024 * 1024)  # [] and trailing zeros
def vmsize():
    for line in open("/proc/self/status"):
        if line.startswith("VmSize:"):
            return int(line.split()[1]) * 1024
soft, hard = resource.getrlimit(resource.RLIMIT_AS)
resource.setrlimit(resource.RLIMIT_AS, (vmsize() + 64 * 1024 * 1024, hard))  # the 256 MiB copy can't fit
try:
    btoon.decode(ba)
    print("decoded")
except MemoryError:
    print("MemoryError")
finally:
    resource.setrlimit(resource.RLIMIT_AS, (soft, hard))
ba.append(0)  # BufferError if decode still holds an export of ba
print("released")
'''


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="needs RLIMIT_AS and /proc")
def test_failed_input_copy_releases_the_buffer():
    # Review 2026-11-13: when copying a bytearray input threw, its buffer export leaked and
    # the bytearray could never be resized again.
    out = _run(_COPY_FAILS)
    assert out.stdout.split() == ["MemoryError", "released"], out.stderr[-2000:]


_PEAK = r'''
import resource, sys, btoon
n = 24
item = b"\xc6" + (4 << 20).to_bytes(4, "big") + b"\xab" * (4 << 20)
data = b"".join([b"\xdc" + n.to_bytes(2, "big")] + [item] * n)  # one allocation: no 2x transient
del item
scale = 1 if sys.platform == "darwin" else 1024
before = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * scale
out = btoon.decode(data)
after = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * scale
assert len(out) == n and all(len(b) == 4 << 20 and b[:1] == b"\xab" for b in out)
print((after - before) / (n * (4 << 20)))
'''


@pytest.mark.skipif(not sys.platform.startswith("linux"),
                    reason="ru_maxrss counts pages macOS hasn't reclaimed yet; no resource module on Windows")
def test_decode_releases_cores_tree_while_converting():
    # Review 2026-11-13: the whole decoded tree stayed alive until every Python object
    # existed, so the peak was the tree plus the objects (2x the payload here). Each part
    # of the tree is now freed once converted: the peak is about the payload plus one blob.
    out = _run(_PEAK)
    assert out.returncode == 0, out.stderr[-2000:]
    growth = float(out.stdout)
    assert growth < 1.5, f"decoding grew the peak RSS by {growth:.2f}x the payload"


def test_extreme_timestamps_fall_back_without_overflow():
    # Review 2026-11-13: seconds near ±2**63 plus an offset overflowed int64 (UB, seen under
    # UBSan) before the out-of-range check; those instants now fall back to Timestamp first.
    def ts14(sec, off, nanos=0):
        return b"\xc7\x0e\xff" + struct.pack(">IqH", nanos, sec, off & 0xFFFF)

    for sec, off in [(2**63 - 1, 1080), (-(2**63), -1080), (-(2**63), 0), (2**63 - 1, -1080), (2**63 - 1, 0)]:
        assert btoon.decode(ts14(sec, off)) == btoon.Timestamp(sec, 0, off)
    ts12 = b"\xc7\x0c\xff" + struct.pack(">Iq", 999_999_999, -(2**63))
    assert btoon.decode(ts12) == btoon.Timestamp(-(2**63), 999_999_999)
    # Just inside datetime's range, an offset still gives an aware datetime.
    edge = btoon.Timestamp(253402300799, 0, -1080)  # 9999-12-31T23:59:59Z, shown at -18:00
    assert btoon.decode(btoon.encode(edge)).isoformat() == "9999-12-31T05:59:59-18:00"


def test_long_arrays_and_tables_convert_correctly():
    # The conversion frees core's arrays back to front and shrinks them as they empty.
    doc = {"ints": list(range(20000)), "rows": [{"id": i, "s": str(i), "b": [i, {"k": i}]} for i in range(9000)],
           "mixed": [None, True, 1.5, "x" * 300, b"y" * 300, [[]], {}] * 1000}
    for options in ({}, {"timestamps": "object", "vectors": "list"}):
        assert btoon.decode(btoon.encode(doc), **options) == doc
