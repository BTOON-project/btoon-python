"""Python-specific behaviour of the btoon package."""

import array
import datetime as dt
import math
import pickle
import threading

import pytest

import btoon
from btoon import BtoonError, BtoonTypeError, ExtType, Timestamp

UTC = dt.timezone.utc


def roundtrip(v, **kw):
    return btoon.decode(btoon.encode(v, **kw))


# --- types -----------------------------------------------------------------------------

@pytest.mark.parametrize("v", [None, True, False, 0, 1, -1, 127, 128, -33, 2**31, 2**63 - 1, 2**63, 2**64 - 1,
                               -(2**63), 1.5, -0.0, "", "héllo 🎉", b"", b"\x00\xff", [], {}, [1, [2, [3]]],
                               {"a": {"b": [None]}}])
def test_roundtrip_basic(v):
    assert roundtrip(v) == v


@pytest.mark.parametrize("v", [2**64, -(2**63) - 1, 10**40, -(10**40)])
def test_big_integers(v):
    enc = btoon.encode(v)
    assert enc[0] == 0xC7 and enc[2] == 0xFC  # bigint extension
    assert btoon.decode(enc) == v


def test_tuple_and_bytes_likes():
    assert roundtrip((1, 2, (3,))) == [1, 2, [3]]
    assert roundtrip(bytearray(b"ab")) == b"ab"
    assert roundtrip(memoryview(b"xyz")) == b"xyz"


def test_nan_and_infinities():
    assert math.isnan(roundtrip(math.nan))
    assert roundtrip(math.inf) == math.inf
    assert btoon.encode(float("nan")) == bytes.fromhex("cb7ff8000000000000")


def test_datetime_roundtrip_is_utc_aware():
    t = dt.datetime(2024, 1, 1, 12, 30, 15, 123456, tzinfo=UTC)
    out = roundtrip(t)
    assert out == t and out.tzinfo is not None
    # aware datetimes in other zones are normalized to UTC (same instant)
    plus2 = dt.datetime(2024, 1, 1, 14, 30, 15, 123456, tzinfo=dt.timezone(dt.timedelta(hours=2)))
    assert roundtrip(plus2) == t
    # naive datetimes are taken to be UTC
    assert roundtrip(t.replace(tzinfo=None)) == t


def test_timestamp_objects_keep_nanoseconds_and_offset():
    ts = Timestamp(1704067200, 123456789, offset_minutes=-330)
    assert btoon.decode(btoon.encode(ts), timestamps="object") == ts
    as_dt = btoon.decode(btoon.encode(ts))
    assert as_dt.utcoffset() == dt.timedelta(minutes=-330)
    assert as_dt == ts.to_datetime()
    assert Timestamp.from_datetime(as_dt, keep_offset=True) == Timestamp(1704067200, 123456000, -330)


def test_out_of_range_timestamp_falls_back_to_object():
    far = Timestamp(2**40)
    assert btoon.decode(btoon.encode(far)) == far


def test_vectors():
    f = array.array("f", [1.5, -2.0])
    d = array.array("d", [0.1, 2.5])
    assert roundtrip(f) == f and roundtrip(d) == d
    assert btoon.decode(btoon.encode(f), vectors="list") == [1.5, -2.0]
    assert btoon.encode(f) == bytes.fromhex("d7fb0000c03f000000c0")


def test_numpy_vectors():
    np = pytest.importorskip("numpy")
    v = np.array([0.25, -1.0, 3.5], dtype=np.float32)
    out = btoon.decode(btoon.encode(v), vectors="numpy")
    assert out.dtype == np.float32 and (out == v).all()
    m = np.arange(6).reshape(2, 3)
    assert roundtrip(m) == [[0, 1, 2], [3, 4, 5]]


def test_ext_type():
    e = ExtType(42, b"\x01\x02")
    assert roundtrip(e) == e
    with pytest.raises(ValueError):
        ExtType(200, b"")


def test_dicts_with_type_and_data_keys_stay_dicts():
    # 0.0.x silently turned {"type": .., "data": ..} into an extension
    v = {"type": 1, "data": b"x"}
    assert roundtrip(v) == v


def test_unsupported_type_and_default_hook():
    class Point:
        def __init__(self, x, y):
            self.x, self.y = x, y

    with pytest.raises(TypeError) as exc:
        btoon.encode({"p": Point(1, 2)})
    assert exc.value.code == "invalid_argument"
    out = roundtrip({"p": Point(1, 2)}, default=lambda o: {"x": o.x, "y": o.y})
    assert out == {"p": {"x": 1, "y": 2}}
    with pytest.raises(BtoonError):
        btoon.encode(Point(0, 0), default=lambda o: o)  # default() that never resolves terminates


def test_non_string_keys_rejected():
    with pytest.raises(BtoonTypeError):
        btoon.encode({1: "a"})


def test_cycles_raise_instead_of_crashing():
    a = []
    a.append(a)
    with pytest.raises(BtoonError) as exc:
        btoon.encode(a)
    assert exc.value.code == "max_depth"
    d = {}
    d["self"] = d
    with pytest.raises(BtoonError):
        btoon.encode(d)


def test_lone_surrogate_is_an_error():
    with pytest.raises(UnicodeEncodeError):
        btoon.encode("\ud800")


# --- tabular, compression, signing ----------------------------------------------------

ROWS = [{"id": i, "name": f"user{i}", "email": f"u{i}@example.com", "active": i % 2 == 0} for i in range(100)]


def test_tabular_is_smaller_and_lossless():
    tab = btoon.encode(ROWS)
    plain = btoon.encode(ROWS, auto_tabular=False)
    assert len(tab) < 0.7 * len(plain)
    assert btoon.decode(tab) == ROWS == btoon.decode(plain)


def test_decoded_tables_share_key_objects():
    rows = btoon.decode(btoon.encode(ROWS))
    k0 = next(iter(rows[0]))
    k1 = next(iter(rows[1]))
    assert k0 is k1


def test_compression_defaults_to_zlib_and_auto_decompresses():
    packed = btoon.encode(ROWS, compress=True)
    assert packed[:4] == b"BTON" and packed[5] == 0
    assert len(packed) < len(btoon.encode(ROWS)) / 2
    assert btoon.decode(packed) == ROWS
    with pytest.raises(BtoonError) as exc:
        btoon.decode(packed, auto_decompress=False)
    assert exc.value.code == "invalid_envelope"


def test_deprecated_decompress_kwarg_still_works():
    packed = btoon.encode(ROWS, compress=True)
    with pytest.warns(DeprecationWarning):
        assert btoon.decode(packed, decompress=False) == ROWS


def test_unavailable_compression_is_an_error():
    if btoon.compression_available("snappy"):
        pytest.skip("snappy built in")
    with pytest.raises(BtoonError) as exc:
        btoon.encode(ROWS, compress=True, compression="snappy")
    assert exc.value.code == "unsupported_compression"
    with pytest.raises(TypeError):
        btoon.encode(1, compress=True, compression="bogus")


def test_decompression_limit():
    big = {"blob": b"\x00" * (8 << 20)}
    packed = btoon.encode(big, compress=True)
    with pytest.raises(BtoonError) as exc:
        btoon.decode(packed, max_decompressed_size=1 << 20)
    assert exc.value.code == "size_limit"


def test_hmac_signing():
    key = b"k" * 32
    sealed = btoon.encode(ROWS, hmac_key=key, compress=True)
    assert sealed[:4] == b"BTSG"
    assert btoon.decode(sealed, hmac_key=key) == ROWS
    assert btoon.decode(sealed, hmac_key=key.decode()) == ROWS  # str keys are UTF-8 encoded
    for bad in ({}, {"hmac_key": b"x" * 32}):
        with pytest.raises(BtoonError) as exc:
            btoon.decode(sealed, **bad)
        assert exc.value.code == "authentication_failed"
    for empty in (b"", ""):  # an empty key is a configuration error, not "no key"
        with pytest.raises(BtoonTypeError):
            btoon.decode(btoon.encode(1), hmac_key=empty)
        with pytest.raises(BtoonTypeError):
            btoon.encode(1, hmac_key=empty)


# --- hostile input -------------------------------------------------------------------------

@pytest.mark.parametrize("hexdata,code", [
    ("81", "truncated"), ("92" "01", "truncated"), ("ddffffffff", "truncated"),
    ("0102", "trailing_data"), ("a1ff", "invalid_utf8"), ("c1", "invalid_marker"),
    ("810101", "non_string_key"), ("82a16101a16102", "duplicate_key"),
])
def test_error_codes(hexdata, code):
    with pytest.raises(BtoonError) as exc:
        btoon.decode(bytes.fromhex(hexdata))
    assert exc.value.code == code


def test_deep_nesting_is_rejected_quickly():
    with pytest.raises(BtoonError) as exc:
        btoon.decode(b"\x91" * 1_000_000 + b"\xc0")
    assert exc.value.code == "max_depth"
    assert btoon.decode(b"\x91" * 10 + b"\xc0", max_depth=10) is not None


def test_non_strict_mode():
    assert btoon.decode(b"\x01\x02", strict=False) == 1
    assert btoon.decode(bytes.fromhex("82a16101a16102"), strict=False) == {"a": 2}


def test_decode_accepts_bytes_likes():
    raw = btoon.encode({"a": 1})
    assert btoon.decode(bytearray(raw)) == {"a": 1}
    assert btoon.decode(memoryview(raw)) == {"a": 1}
    with pytest.raises(TypeError):
        btoon.decode("not bytes")


def test_errors_are_picklable():
    e = BtoonError("truncated", "boom")
    e2 = pickle.loads(pickle.dumps(e))
    assert e2.code == "truncated" and str(e2) == "boom"


def test_threads():
    # encode/decode release the GIL around the native work; results must stay correct.
    data = btoon.encode(ROWS)
    errors = []

    def work():
        try:
            for _ in range(200):
                assert btoon.decode(data) == ROWS
                assert btoon.encode(ROWS) == data
        except Exception as exc:  # pragma: no cover - surfaced below
            errors.append(exc)

    threads = [threading.Thread(target=work) for _ in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert not errors


# --- MessagePack interoperability ------------------------------------------------------------

def test_msgpack_reads_btoon_and_vice_versa():
    msgpack = pytest.importorskip("msgpack")
    doc = {"b": [1, 2.5, None, True], "a": {"x": "y", "bin": b"\x01"}}
    # BTOON without tabular is canonical MessagePack (sorted keys)
    assert msgpack.unpackb(btoon.encode(doc, auto_tabular=False)) == doc
    assert btoon.decode(msgpack.packb(doc)) == doc
    ts = msgpack.Timestamp(1704067200, 500)
    assert btoon.decode(msgpack.packb(ts), timestamps="object") == Timestamp(1704067200, 500)
    assert msgpack.unpackb(btoon.encode(Timestamp(1704067200, 500))) == ts


def test_version_info():
    assert btoon.__version__ == "0.1.0"
    assert btoon.SPEC_VERSION == "0.1"
    assert btoon.version() == "0.1.0"
