"""Conformance: every vector in btoon-core/tests/vectors/btoon-vectors.json (spec §10)."""

import array
import inspect
import json
import math
import os
import struct
from pathlib import Path

import pytest

import btoon
from btoon import Date, DateTime, ExtType, Timestamp


ROOT = Path(__file__).resolve().parent.parent


def vectors_path() -> Path:
    for c in (
        os.environ.get("BTOON_VECTORS"),
        ROOT / "core" / "tests" / "vectors" / "btoon-vectors.json",
        ROOT.parent / "btoon-core" / "tests" / "vectors" / "btoon-vectors.json",
    ):
        if c and Path(c).is_file():
            return Path(c)
    raise FileNotFoundError("btoon-vectors.json not found: run `git submodule update --init` or set BTOON_VECTORS")


VECTORS = json.loads(vectors_path().read_text(encoding="utf-8"))

# Float vector elements (spec §10): "NaN" is the canonical quiet NaN of the width, "0x…" is
# an exact bit pattern (vectors keep NaN payloads, §5.5), anything else is a decimal.
_BITS = {"f": "I", "d": "Q"}
_CANONICAL_NAN = {"f": 0x7FC00000, "d": 0x7FF8000000000000}


def element_bits(s, typecode):
    if s.startswith("0x"):
        return int(s, 16)
    if s == "NaN":
        return _CANONICAL_NAN[typecode]
    x = float(s)  # a decimal, "Infinity" or "-Infinity"
    try:
        packed = struct.pack("<" + typecode, x)
    except OverflowError:  # beyond binary32's range: rounds to ±inf, like a C cast
        packed = struct.pack("<" + typecode, math.copysign(math.inf, x))
    return struct.unpack("<" + _BITS[typecode], packed)[0]


def float_vector(elements, typecode):
    bits = [element_bits(e, typecode) for e in elements]
    return array.array(typecode, struct.pack(f"={len(bits)}{_BITS[typecode]}", *bits))


def from_typed(t):
    (kind, x), = t.items()
    if kind == "nil":
        return None
    if kind == "bool":
        return x
    if kind == "int":
        return int(x)
    if kind == "float":
        return float(x)  # accepts "NaN", "Infinity", "-Infinity"
    if kind == "string":
        return x
    if kind == "binary":
        return bytes.fromhex(x)
    if kind == "array":
        return [from_typed(e) for e in x]
    if kind == "map":
        return {k: from_typed(e) for k, e in x.items()}
    if kind == "timestamp":
        return Timestamp(int(x["seconds"]), x["nanoseconds"], x.get("offset_minutes"))
    if kind == "date":
        return Date(int(x))
    if kind == "datetime":
        return DateTime(int(x))
    if kind == "float32_vector":
        return float_vector(x, "f")
    if kind == "float64_vector":
        return float_vector(x, "d")
    if kind == "ext":
        return ExtType(x["type"], bytes.fromhex(x["data"]))
    raise ValueError(kind)


def contains(t, kinds):
    (kind, x), = t.items()
    if kind in kinds:
        return True
    if kind == "array":
        return any(contains(e, kinds) for e in x)
    if kind == "map":
        return any(contains(e, kinds) for e in x.values())
    return False


def same(a, b):
    if isinstance(a, float) and isinstance(b, float):
        if math.isnan(a) or math.isnan(b):
            return math.isnan(a) and math.isnan(b)
        return a == b and math.copysign(1, a) == math.copysign(1, b)
    if isinstance(a, array.array) and isinstance(b, array.array):
        # Vectors are opaque buffers: elements compare bit for bit, NaN payloads included.
        return a.typecode == b.typecode and a.tobytes() == b.tobytes()
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
    if type(a) is not type(b) and not (isinstance(a, int) and isinstance(b, int)):
        return False
    return a == b


DECODE_OPTIONS = ("strict", "max_depth", "max_decompressed_size", "max_values")


def decode_options(v):
    """The decoder options a vector sets (spec §10). The rest keep the file's defaults."""
    opts = v.get("options", {})
    unknown = set(opts) - {"hmac_key", *DECODE_OPTIONS}
    assert not unknown, f"vector option(s) this harness doesn't know: {sorted(unknown)}"
    out = {name: opts[name] for name in DECODE_OPTIONS if name in opts}
    if "hmac_key" in opts:
        out["hmac_key"] = bytes.fromhex(opts["hmac_key"])  # "" is an empty key: invalid_argument
    return out


def encode_options(v):
    """Re-encoding a signed vector signs with its key."""
    opts = v.get("options", {})
    return {"hmac_key": bytes.fromhex(opts["hmac_key"])} if "hmac_key" in opts else {}


def ids(vs):
    return [v["id"] for v in vs]


def test_spec_version():
    assert VECTORS["spec"] == btoon.SPEC_VERSION


def test_defaults_are_the_spec_defaults():
    # Vectors without options use the file's defaults (spec §10, §7.1): decode's must match.
    params = inspect.signature(btoon.decode).parameters
    for name, value in VECTORS["defaults"].items():
        assert params[name].default == value, name


def kind_of(v):
    return "refused" if "encode_error" in v else "invalid" if "error" in v else "valid"


def _cases(vectors, kind):
    out = []
    for v in vectors["vectors"]:
        if kind_of(v) != kind:
            continue
        req = v.get("requires")
        marks = []
        if req and not btoon.compression_available(req):
            marks.append(pytest.mark.skip(reason=f"{req} not built"))
        out.append(pytest.param(v, id=v["id"], marks=marks))
    return out


def pytest_generate_tests(metafunc):
    for kind in ("valid", "invalid", "refused"):
        if kind in metafunc.fixturenames:
            metafunc.parametrize(kind, _cases(VECTORS, kind))


def test_decode(valid):
    data = bytes.fromhex(valid["hex"])
    got = btoon.decode(data, timestamps="object", **decode_options(valid))
    assert same(got, from_typed(valid["value"])), f"decoded {got!r}"


def test_canonical_encode(valid):
    # spec §10 (0.3): every canonical vector re-encodes, date and datetime included.
    if not valid["canonical"]:
        pytest.skip("decode-only vector")
    value = from_typed(valid["value"])
    assert btoon.encode(value, **encode_options(valid)).hex() == valid["hex"]


def test_decode_then_encode_is_lossless(valid):
    # timestamps="object" preserves every BTOON type (spec §10), so a canonical document
    # survives decode + encode byte for byte.
    if not valid["canonical"]:
        pytest.skip("decode-only vector")
    data = bytes.fromhex(valid["hex"])
    value = btoon.decode(data, timestamps="object", **decode_options(valid))
    assert btoon.encode(value, **encode_options(valid)) == data


def test_harness_covers_the_typed_instants():
    # The 0.3 vectors that need btoon.Date/btoon.DateTime to re-encode are all canonical.
    ids = {v["id"] for v in VECTORS["vectors"] if v.get("canonical") and contains(v["value"], {"date", "datetime"})}
    assert {"date/max", "date/min", "datetime/max", "datetime/min", "tabular/date-and-datetime-columns"} <= ids


def test_rejects(invalid):
    with pytest.raises(btoon.BtoonError) as exc:
        btoon.decode(bytes.fromhex(invalid["hex"]), **decode_options(invalid))
    assert exc.value.code == invalid["error"], str(exc.value)


def test_encode_refuses(refused):
    # spec §10: encoders MUST refuse an encode_error vector's value, with that code.
    with pytest.raises(btoon.BtoonError) as exc:
        btoon.encode(from_typed(refused["value"]), **encode_options(refused))
    assert exc.value.code == refused["encode_error"], str(exc.value)
