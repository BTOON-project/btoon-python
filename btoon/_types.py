"""Value types and errors shared by the Python layer and the native module."""

from __future__ import annotations

import datetime as _dt
from dataclasses import dataclass
from collections import namedtuple

__all__ = ["BtoonError", "BtoonTypeError", "ExtType", "Timestamp"]


class BtoonError(ValueError):
    """Raised when BTOON data is invalid or an operation can't be performed.

    ``code`` is the machine-readable error code from the BTOON spec (§7), e.g.
    ``"truncated"``, ``"max_depth"``, ``"invalid_utf8"``, ``"authentication_failed"``.
    """

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code

    def __reduce__(self):  # keep picklable despite the two-argument constructor
        return (type(self), (self.code, str(self)))


class BtoonTypeError(BtoonError, TypeError):
    """Raised when a Python value can't be encoded (``code == "invalid_argument"``)."""


class ExtType(namedtuple("ExtType", ["code", "data"])):
    """An extension value BTOON doesn't interpret: type ``code`` (-128..127) plus raw ``data`` bytes.

    Codes -1 to -6 and -10 are BTOON's own types (timestamp, date, datetime, bigint, the
    float vectors, tabular). Decoding never returns them as ``ExtType``, and encoding an
    ``ExtType`` with one of them raises :class:`BtoonError` (``invalid_argument``, spec
    §5.7), because a payload written verbatim could be malformed or non-canonical. The
    ids reserved for future versions (-7 to -9, -11 to -128) are written as given.
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


@dataclass(frozen=True, slots=True)
class Timestamp:
    """A BTOON timestamp with nanosecond precision and an optional UTC offset.

    ``seconds``/``nanoseconds`` always describe the instant in UTC. ``offset_minutes``
    (when not None) records the local UTC offset and is written with BTOON's 14-byte
    timestamp form. Decoding returns ``datetime`` objects by default; pass
    ``timestamps="object"`` to get ``Timestamp`` instead (no precision loss).
    """

    seconds: int
    nanoseconds: int = 0
    offset_minutes: int | None = None

    def __post_init__(self) -> None:
        if not 0 <= self.nanoseconds < 1_000_000_000:
            raise ValueError("nanoseconds must be in [0, 1e9)")
        if self.offset_minutes is not None and not -1080 <= self.offset_minutes <= 1080:
            raise ValueError("offset_minutes must be within ±1080")
        if not -(2**63) <= self.seconds < 2**63:
            raise ValueError("seconds must fit in a signed 64-bit integer")

    @classmethod
    def from_datetime(cls, dt: _dt.datetime, *, keep_offset: bool = False) -> Timestamp:
        """Converts a datetime (naive values are taken as UTC); optionally keeps its offset."""
        aware = dt if dt.tzinfo is not None else dt.replace(tzinfo=_dt.timezone.utc)
        delta = aware - _EPOCH
        micros = (delta.days * 86_400 + delta.seconds) * 1_000_000 + delta.microseconds
        seconds, rem = divmod(micros, 1_000_000)
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
