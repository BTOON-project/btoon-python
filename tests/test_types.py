"""btoon.Date and btoon.DateTime (spec 0.3 §10): every BTOON instant type is writable."""

import copy
import dataclasses
import datetime as dt
import pickle
import struct

import pytest

import btoon
from btoon import BtoonError, BtoonTypeError, Date, DateTime, ExtType, Timestamp

UTC = dt.timezone.utc
I64_MIN, I64_MAX = -(2**63), 2**63 - 1


def fixext8(code, value):
    return bytes([0xD7, code & 0xFF]) + struct.pack(">q", value)


# --- the value types --------------------------------------------------------------------

def test_date_fields_and_repr():
    d = Date(1700000000123)
    assert d.epoch_milliseconds == 1700000000123
    assert (d.seconds, d.nanoseconds, d.offset_minutes) == (1700000000, 123_000_000, None)
    assert repr(d) == "btoon.Date(1700000000123)"
    assert isinstance(d, Timestamp)
    assert Date(epoch_milliseconds=5) == Date(5)


def test_datetime_fields_and_repr():
    d = DateTime(1700000000123456789)
    assert d.epoch_nanoseconds == 1700000000123456789
    assert (d.seconds, d.nanoseconds, d.offset_minutes) == (1700000000, 123456789, None)
    assert repr(d) == "btoon.DateTime(1700000000123456789)"
    assert isinstance(d, Timestamp)


def test_negative_values_floor():
    assert (Date(-1).seconds, Date(-1).nanoseconds) == (-1, 999_000_000)
    assert (DateTime(-1).seconds, DateTime(-1).nanoseconds) == (-1, 999_999_999)
    assert Date(I64_MIN).seconds == -9223372036854776 and Date(I64_MIN).nanoseconds == 192_000_000


@pytest.mark.parametrize("cls", [Date, DateTime])
def test_range_and_type_are_checked(cls):
    assert cls(I64_MAX) and cls(I64_MIN)
    for bad in (2**63, I64_MIN - 1, 10**30):
        with pytest.raises(ValueError):  # the same type Timestamp raises for its range
            cls(bad)
    with pytest.raises(ValueError):
        Timestamp(2**63)
    for bad in (1.5, "1", None, 1j):
        with pytest.raises(TypeError):
            cls(bad)
    assert cls(True) == cls(1)  # ints by __index__, like Timestamp


def test_equality_distinguishes_the_types():
    assert Date(0) != Timestamp(0) and Timestamp(0) != Date(0)
    assert DateTime(0) != Timestamp(0) and Date(0) != DateTime(0)
    assert Date(1000) != DateTime(1_000_000_000)  # the same instant, different BTOON values
    assert Date(7) == Date(7) and DateTime(7) == DateTime(7)
    assert len({Date(1), Date(1), DateTime(1), Timestamp(0, 1_000_000)}) == 3
    assert hash(Date(42)) == hash(Date(42))


@pytest.mark.parametrize("value", [Date(1700000000123), DateTime(-1), Date(I64_MIN), DateTime(I64_MAX)])
def test_pickle_and_copy(value):
    for protocol in range(pickle.HIGHEST_PROTOCOL + 1):
        again = pickle.loads(pickle.dumps(value, protocol))
        assert again == value and type(again) is type(value)
    assert copy.copy(value) == value and copy.deepcopy(value) == value


def test_frozen():
    d = Date(5)
    with pytest.raises(dataclasses.FrozenInstanceError):
        d.epoch_milliseconds = 6
    with pytest.raises(dataclasses.FrozenInstanceError):
        d.seconds = 6
    assert not hasattr(d, "__dict__")


@pytest.mark.parametrize("cls,field", [(Date, "epoch_milliseconds"), (DateTime, "epoch_nanoseconds")])
def test_replace(cls, field):
    # Documented: replace() takes the epoch value; the inherited fields are derived from it.
    v = cls(5)
    assert dataclasses.replace(v, **{field: 7}) == cls(7)
    assert dataclasses.replace(v) == v
    for derived in ("seconds", "nanoseconds", "offset_minutes"):
        with pytest.raises((TypeError, ValueError), match="init=False"):
            dataclasses.replace(v, **{derived: 1})
    if hasattr(copy, "replace"):  # Python 3.13+
        assert copy.replace(v, **{field: 9}) == cls(9)
        with pytest.raises((TypeError, ValueError), match="init=False"):
            copy.replace(v, seconds=1)


def test_from_datetime():
    aware = dt.datetime(2023, 11, 14, 22, 13, 20, 123999, tzinfo=UTC)
    assert Date.from_datetime(aware) == Date(1700000000123)  # rounds down to the millisecond
    assert DateTime.from_datetime(aware) == DateTime(1700000000123999000)  # exact
    assert Date.from_datetime(aware.replace(tzinfo=None)) == Date(1700000000123)  # naive is UTC
    plus2 = aware.astimezone(dt.timezone(dt.timedelta(hours=2)))
    assert Date.from_datetime(plus2) == Date(1700000000123)
    before_epoch = dt.datetime(1969, 12, 31, 23, 59, 59, 999999, tzinfo=UTC)
    assert Date.from_datetime(before_epoch) == Date(-1)  # floor, not toward zero
    assert DateTime.from_datetime(before_epoch) == DateTime(-1000)
    with pytest.raises(ValueError):  # datetime.max is past 2262, the end of DateTime's range
        DateTime.from_datetime(dt.datetime.max)
    assert Date.from_datetime(dt.datetime.max).to_datetime() == dt.datetime.max.replace(microsecond=999000, tzinfo=UTC)
    with pytest.raises(TypeError):
        Date.from_datetime(dt.date(2024, 1, 1))


def test_from_timestamp():
    assert Date.from_timestamp(Timestamp(1, 999_999_999)) == Date(1999)
    assert Date.from_timestamp(Timestamp(-1, 1)) == Date(-1000)  # floors
    assert DateTime.from_timestamp(Timestamp(-1, 1)) == DateTime(-999_999_999)  # exact
    assert Date.from_timestamp(DateTime(1_500_000)) == Date(1)
    for cls in (Date, DateTime):
        with pytest.raises(ValueError, match="offset"):
            cls.from_timestamp(Timestamp(0, 0, 60))
        with pytest.raises(ValueError):
            cls.from_timestamp(Timestamp(I64_MAX))  # out of range in the finer unit
        with pytest.raises(TypeError):
            cls.from_timestamp(0)


def test_to_datetime_is_inherited():
    assert Date(1700000000123).to_datetime() == dt.datetime(2023, 11, 14, 22, 13, 20, 123000, tzinfo=UTC)
    assert DateTime(1700000000123456789).to_datetime() == dt.datetime(2023, 11, 14, 22, 13, 20, 123456, tzinfo=UTC)


def test_exported_and_typed():
    assert {"Date", "DateTime"} <= set(btoon.__all__)
    assert btoon.Date is Date and btoon.DateTime is DateTime
    hints = Date.__init__.__annotations__
    assert hints["epoch_milliseconds"] in (int, "int")


# --- encoding ---------------------------------------------------------------------------------

@pytest.mark.parametrize("ms", [0, 1, -1, 1700000000123, I64_MIN, I64_MAX])
def test_date_encodes_as_extension_minus_2(ms):
    data = btoon.encode(Date(ms))
    assert data == fixext8(-2, ms)
    assert btoon.decode(data, timestamps="object") == Date(ms)


@pytest.mark.parametrize("ns", [0, 1, -1, 1700000000123456789, I64_MIN, I64_MAX])
def test_datetime_encodes_as_extension_minus_3(ns):
    data = btoon.encode(DateTime(ns))
    assert data == fixext8(-3, ns)
    assert btoon.decode(data, timestamps="object") == DateTime(ns)


def test_timestamps_and_datetimes_still_write_minus_1():
    assert btoon.encode(Timestamp(1700000000)) == bytes.fromhex("d6ff6553f100")
    assert btoon.encode(dt.datetime(2024, 1, 1, tzinfo=UTC)) == bytes.fromhex("d6ff65920080")


def test_subclasses_encode_as_their_base():
    class MyDate(Date):
        pass

    assert btoon.encode(MyDate(5)) == btoon.encode(Date(5))


def test_tampered_values_raise_btoon_errors():
    # Only object.__setattr__ can get past the constructors' checks; the encoder still
    # refuses with a BtoonError instead of crashing or writing garbage.
    d = Date(5)
    object.__setattr__(d, "epoch_milliseconds", 2**70)
    with pytest.raises(BtoonError) as exc:
        btoon.encode(d)
    assert exc.value.code == "invalid_argument" and not isinstance(exc.value, TypeError)
    object.__setattr__(d, "epoch_milliseconds", "5")
    with pytest.raises(BtoonTypeError):
        btoon.encode(d)
    t = Timestamp(5)
    object.__setattr__(t, "nanoseconds", 10**9)
    with pytest.raises(BtoonError) as exc:
        btoon.encode(t)
    assert exc.value.code == "invalid_argument"
    object.__setattr__(t, "nanoseconds", 1.5)
    with pytest.raises(BtoonTypeError):
        btoon.encode(t)


@pytest.mark.parametrize("code,name", [(-2, "btoon.Date"), (-3, "btoon.DateTime")])
def test_raw_date_extensions_stay_refused(code, name):
    with pytest.raises(BtoonError) as exc:
        btoon.encode(ExtType(code, b"\x00" * 8))
    assert exc.value.code == "invalid_argument"
    assert name in str(exc.value) and "§5.7" in str(exc.value)


def test_tables_of_dates():
    rows = [{"day": Date(1700000000000 + i * 86_400_000), "at": DateTime(1700000000123456789 + i), "id": i}
            for i in range(6)]
    data = btoon.encode(rows)
    assert data[0] == 0xC7 and data[2] == 0xF6  # tabular: they are ordinary cells
    assert btoon.encode(rows, auto_tabular=False)[0] == 0x96
    assert btoon.decode(data, timestamps="object") == rows
    # The rule only looks at the row maps, not at the cells (spec §5.6.2).
    plain = [{"day": i, "at": i, "id": i} for i in range(6)]
    assert (btoon.encode(plain)[0] == 0xC7) == (data[0] == 0xC7)


# --- decoding ---------------------------------------------------------------------------------

def test_object_mode_returns_the_typed_values():
    doc = {"t": Timestamp(1, 2), "d": Date(3), "n": DateTime(4)}
    out = btoon.decode(btoon.encode(doc), timestamps="object")
    assert out == doc
    assert [type(out[k]) for k in ("t", "d", "n")] == [Timestamp, Date, DateTime]


def test_datetime_mode_is_unchanged_in_range():
    doc = [Date(1700000000123), DateTime(1700000000123456789), DateTime(-1)]
    assert btoon.decode(btoon.encode(doc)) == [
        dt.datetime(2023, 11, 14, 22, 13, 20, 123000, tzinfo=UTC),
        dt.datetime(2023, 11, 14, 22, 13, 20, 123456, tzinfo=UTC),
        dt.datetime(1969, 12, 31, 23, 59, 59, 999999, tzinfo=UTC),
    ]


def test_datetime_mode_falls_back_to_the_typed_value():
    # Outside datetime's years 1-9999 a date stays a btoon.Date (it used to become a
    # Timestamp, losing the type); timestamps still fall back to Timestamp.
    for value in (Date(I64_MAX), Date(I64_MIN), Date(-62135596800001)):
        assert btoon.decode(btoon.encode(value)) == value
    assert btoon.decode(btoon.encode(Date(-62135596800000))) == dt.datetime(1, 1, 1, tzinfo=UTC)
    assert type(btoon.decode(btoon.encode(Timestamp(2**40)))) is Timestamp
    # DateTime's whole range (1677-2262) fits in datetime.
    assert btoon.decode(btoon.encode(DateTime(I64_MIN))) == dt.datetime(1677, 9, 21, 0, 12, 43, 145224, tzinfo=UTC)
