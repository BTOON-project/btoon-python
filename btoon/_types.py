"""Value types and errors shared by the Python layer and the native module."""

from __future__ import annotations

import datetime as _dt
import operator as _operator
from collections import namedtuple
from dataclasses import dataclass, field

__all__ = ["BtoonError", "BtoonTypeError", "ExtType", "Timestamp", "Date", "DateTime"]


class BtoonError(ValueError):
    """Raised when BTOON data is invalid or an operation can't be performed.

    ``code`` is the machine-readable error code from the BTOON spec (§7), e.g.
    ``"truncated"``, ``"max_depth"``, ``"invalid_utf8"``, ``"authentication_failed"``.
    """

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code

    def __reduce__(self) -> tuple[type[BtoonError], tuple[str, str]]:
        # keep picklable despite the two-argument constructor
        return (type(self), (self.code, str(self)))


class BtoonTypeError(BtoonError, TypeError):
    """Raised when a Python value can't be encoded (``code == "invalid_argument"``)."""


class ExtType(namedtuple("ExtType", ["code", "data"])):
    """An extension value BTOON doesn't interpret: type ``code`` (-128..127) plus raw ``data`` bytes.

    Codes -1 to -6 and -10 are BTOON's own types (timestamp, date, datetime, bigint, the
    float vectors, tabular). Decoding never returns them as ``ExtType``, and encoding an
    ``ExtType`` with one of them raises :class:`BtoonError` (``invalid_argument``, spec
    §5.7), because a payload written verbatim could be malformed or non-canonical. Write
    them from their typed values instead: :class:`Date` and :class:`DateTime` for -2 and
    -3. The ids reserved for future versions (-7 to -9, -11 to -128) are written as given.
    """

    __slots__ = ()
    code: int
    data: bytes

    def __new__(cls, code: int, data: bytes) -> "ExtType":
        if isinstance(code, bool) or not isinstance(code, int) or not -128 <= code <= 127:
            raise ValueError("ExtType code must be an int in -128..127")
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise TypeError("ExtType data must be bytes")
        return super().__new__(cls, code, bytes(data))


_EPOCH = _dt.datetime(1970, 1, 1, tzinfo=_dt.timezone.utc)
_INT64_MIN = -(2**63)
_INT64_MAX = 2**63 - 1


def _integer(name: str, value: object) -> int:
    """``value`` as an int (via ``__index__``, so numpy integers work); floats are refused."""
    try:
        return _operator.index(value)  # type: ignore[arg-type]
    except TypeError:
        raise TypeError(f"{name} must be an int, not {type(value).__name__}") from None


def _int64(name: str, value: object) -> int:
    n = _integer(name, value)
    if not _INT64_MIN <= n <= _INT64_MAX:
        raise ValueError(f"{name} must fit in a signed 64-bit integer")
    return n


def _epoch_microseconds(dt: _dt.datetime) -> int:
    """Microseconds since the epoch. A naive datetime is taken as UTC, as encoding does."""
    if not isinstance(dt, _dt.datetime):
        raise TypeError(f"expected a datetime, not {type(dt).__name__}")
    aware = dt if dt.utcoffset() is not None else dt.replace(tzinfo=_dt.timezone.utc)
    delta = aware - _EPOCH
    return (delta.days * 86_400 + delta.seconds) * 1_000_000 + delta.microseconds


_set = object.__setattr__  # frozen dataclasses: __post_init__ normalizes fields with this


@dataclass(frozen=True, slots=True)
class Timestamp:
    """A BTOON timestamp with nanosecond precision and an optional UTC offset.

    ``seconds``/``nanoseconds`` always describe the instant in UTC. ``offset_minutes``
    (when not None) records the local UTC offset and is written with BTOON's 14-byte
    timestamp form. Decoding returns ``datetime`` objects by default; pass
    ``timestamps="object"`` to get ``Timestamp`` instead (no precision loss).

    The fields must be integers (anything with ``__index__``, such as a numpy integer, is
    converted to ``int``); otherwise the constructor raises ``TypeError``. Out-of-range
    values raise ``ValueError``.
    """

    seconds: int
    nanoseconds: int = 0
    offset_minutes: int | None = None

    def __post_init__(self) -> None:
        seconds = _integer("seconds", self.seconds)
        nanoseconds = _integer("nanoseconds", self.nanoseconds)
        offset = None if self.offset_minutes is None else _integer("offset_minutes", self.offset_minutes)
        if not 0 <= nanoseconds < 1_000_000_000:
            raise ValueError("nanoseconds must be in [0, 1e9)")
        if offset is not None and not -1080 <= offset <= 1080:
            raise ValueError("offset_minutes must be within ±1080")
        if not _INT64_MIN <= seconds <= _INT64_MAX:
            raise ValueError("seconds must fit in a signed 64-bit integer")
        if type(self.seconds) is not int:
            _set(self, "seconds", seconds)
        if type(self.nanoseconds) is not int:
            _set(self, "nanoseconds", nanoseconds)
        if self.offset_minutes is not None and type(self.offset_minutes) is not int:
            _set(self, "offset_minutes", offset)

    @classmethod
    def from_datetime(cls, dt: _dt.datetime, *, keep_offset: bool = False) -> Timestamp:
        """Converts a datetime (naive values are taken as UTC); optionally keeps its offset."""
        seconds, rem = divmod(_epoch_microseconds(dt), 1_000_000)
        offset = None
        if keep_offset and dt.tzinfo is not None:
            off = dt.utcoffset()
            offset = int(off.total_seconds() // 60) if off is not None else None
        return cls(seconds, rem * 1000, offset)

    def to_datetime(self) -> _dt.datetime:
        """Returns an aware datetime (microsecond precision), in its offset if one is set."""
        tz = (
            _dt.timezone.utc
            if self.offset_minutes is None
            else _dt.timezone(_dt.timedelta(minutes=self.offset_minutes))
        )
        return (_EPOCH + _dt.timedelta(seconds=self.seconds, microseconds=self.nanoseconds // 1000)).astimezone(tz)


# Date and DateTime are Timestamp subclasses, so code that checks isinstance(x, Timestamp)
# or reads seconds/nanoseconds keeps working when decode(timestamps="object") returns
# them. They keep the wire value in their own field and fill the inherited ones from it
# (offset_minutes is always None). The inherited fields are init=False, so the
# constructor, dataclasses.replace() and copy.replace() take only the epoch value.


@dataclass(frozen=True, repr=False)
class Date(Timestamp):
    """A BTOON date (extension -2): an instant as milliseconds since the Unix epoch, UTC.

    ``Date(1700000000123)`` encodes as the date extension, and
    ``decode(..., timestamps="object")`` returns one for it, so a date survives a decode and
    re-encode byte for byte. A ``Date`` is a :class:`Timestamp` without an offset, but it
    doesn't compare equal to one: ``Date(0) != Timestamp(0)``, because they are different
    BTOON values. ``epoch_milliseconds`` must fit in a signed 64-bit integer (``ValueError``).

    ``dataclasses.replace(d, epoch_milliseconds=n)`` and ``copy.replace()`` work; the
    inherited fields can't be replaced (they are derived from ``epoch_milliseconds``).
    """

    __slots__ = ("epoch_milliseconds",)

    seconds: int = field(init=False)
    nanoseconds: int = field(init=False)
    offset_minutes: int | None = field(init=False)
    epoch_milliseconds: int

    def __post_init__(self) -> None:
        ms = _int64("epoch_milliseconds", self.epoch_milliseconds)
        seconds, rem = divmod(ms, 1000)
        _set(self, "epoch_milliseconds", ms)
        _set(self, "seconds", seconds)
        _set(self, "nanoseconds", rem * 1_000_000)
        _set(self, "offset_minutes", None)

    @classmethod
    def from_datetime(cls, dt: _dt.datetime) -> Date:  # type: ignore[override]
        """Converts a datetime, rounding down to the millisecond. Naive values are taken as UTC."""
        return cls(_epoch_microseconds(dt) // 1000)

    @classmethod
    def from_timestamp(cls, ts: Timestamp) -> Date:
        """Converts a Timestamp, rounding down to the millisecond.

        A Timestamp with ``offset_minutes`` raises ``ValueError``: a date has no offset.
        """
        _check_no_offset(ts, cls)
        return cls(ts.seconds * 1000 + ts.nanoseconds // 1_000_000)

    def __repr__(self) -> str:
        return f"btoon.Date({self.epoch_milliseconds})"

    def __reduce__(self) -> tuple[type[Date], tuple[int]]:
        return (type(self), (self.epoch_milliseconds,))


@dataclass(frozen=True, repr=False)
class DateTime(Timestamp):
    """A BTOON datetime (extension -3): an instant as nanoseconds since the Unix epoch, UTC.

    The 64-bit range covers 1677-09-21 to 2262-04-11. ``DateTime(1700000000123456789)``
    encodes as the datetime extension, and ``decode(..., timestamps="object")`` returns one
    for it. Like :class:`Date`, it is a :class:`Timestamp` without an offset that doesn't
    compare equal to one. ``epoch_nanoseconds`` must fit in a signed 64-bit integer
    (``ValueError``).

    ``dataclasses.replace(d, epoch_nanoseconds=n)`` and ``copy.replace()`` work; the
    inherited fields can't be replaced (they are derived from ``epoch_nanoseconds``).
    """

    __slots__ = ("epoch_nanoseconds",)

    seconds: int = field(init=False)
    nanoseconds: int = field(init=False)
    offset_minutes: int | None = field(init=False)
    epoch_nanoseconds: int

    def __post_init__(self) -> None:
        ns = _int64("epoch_nanoseconds", self.epoch_nanoseconds)
        seconds, rem = divmod(ns, 1_000_000_000)
        _set(self, "epoch_nanoseconds", ns)
        _set(self, "seconds", seconds)
        _set(self, "nanoseconds", rem)
        _set(self, "offset_minutes", None)

    @classmethod
    def from_datetime(cls, dt: _dt.datetime) -> DateTime:  # type: ignore[override]
        """Converts a datetime exactly. Naive values are taken as UTC."""
        return cls(_epoch_microseconds(dt) * 1000)

    @classmethod
    def from_timestamp(cls, ts: Timestamp) -> DateTime:
        """Converts a Timestamp exactly.

        A Timestamp with ``offset_minutes`` raises ``ValueError``: a datetime has no offset.
        """
        _check_no_offset(ts, cls)
        return cls(ts.seconds * 1_000_000_000 + ts.nanoseconds)

    def __repr__(self) -> str:
        return f"btoon.DateTime({self.epoch_nanoseconds})"

    def __reduce__(self) -> tuple[type[DateTime], tuple[int]]:
        return (type(self), (self.epoch_nanoseconds,))


def _check_no_offset(ts: Timestamp, cls: type) -> None:
    if not isinstance(ts, Timestamp):
        raise TypeError(f"expected a btoon.Timestamp, not {type(ts).__name__}")
    if ts.offset_minutes is not None:
        raise ValueError(
            f"a Timestamp with offset_minutes can't become a btoon.{cls.__name__}, which has no "
            f"UTC offset; drop the offset first: Timestamp(ts.seconds, ts.nanoseconds)"
        )
