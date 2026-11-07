"""Conformance: every vector in btoon-core/tests/vectors/btoon-vectors.json (spec §10)."""

import array
import json
import math
import os
from pathlib import Path

import pytest

import btoon
from btoon import ExtType, Timestamp


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
        s, r = divmod(int(x), 1000)
        return Timestamp(s, r * 1_000_000)
    if kind == "datetime":
        s, r = divmod(int(x), 1_000_000_000)
        return Timestamp(s, r)
    if kind == "float32_vector":
        return array.array("f", [from_typed({"float": e}) for e in x])
    if kind == "float64_vector":
        return array.array("d", [from_typed({"float": e}) for e in x])
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
        return a.typecode == b.typecode and len(a) == len(b) and all(same(float(x), float(y)) for x, y in zip(a, b))
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
    if type(a) is not type(b) and not (isinstance(a, int) and isinstance(b, int)):
        return False
    return a == b


def options(v):
    opts = v.get("options", {})
    return {"hmac_key": bytes.fromhex(opts["hmac_key"])} if "hmac_key" in opts else {}


def ids(vs):
    return [v["id"] for v in vs]


def test_spec_version():
    assert VECTORS["spec"] == btoon.SPEC_VERSION


def _cases(vectors, error):
    out = []
    for v in vectors["vectors"]:
        if ("error" in v) != error:
            continue
        req = v.get("requires")
        marks = []
        if req and not btoon.compression_available(req):
            marks.append(pytest.mark.skip(reason=f"{req} not built"))
        out.append(pytest.param(v, id=v["id"], marks=marks))
    return out


def pytest_generate_tests(metafunc):
    if "valid" in metafunc.fixturenames:
        metafunc.parametrize("valid", _cases(VECTORS, error=False))
    elif "invalid" in metafunc.fixturenames:
        metafunc.parametrize("invalid", _cases(VECTORS, error=True))


def test_decode(valid):
    data = bytes.fromhex(valid["hex"])
    got = btoon.decode(data, timestamps="object", **options(valid))
    assert same(got, from_typed(valid["value"])), f"decoded {got!r}"


def test_canonical_encode(valid):
    if not valid["canonical"]:
        pytest.skip("decode-only vector")
    if contains(valid["value"], {"date", "datetime"}):
        pytest.skip("no Python type maps to the date/datetime extensions (they decode to datetime)")
    value = from_typed(valid["value"])
    assert btoon.encode(value, **options(valid)).hex() == valid["hex"]


def test_rejects(invalid):
    with pytest.raises(btoon.BtoonError) as exc:
        btoon.decode(bytes.fromhex(invalid["hex"]), **options(invalid))
    assert exc.value.code == invalid["error"], str(exc.value)
