"""Encode and decode common Python values with btoon."""

import datetime as dt
import json

import btoon

doc = {
    "name": "Ada",
    "joined": dt.datetime(2024, 1, 1, 9, 30, tzinfo=dt.timezone.utc),
    "big": 2**80,
    "scores": [9.5, 7.25, 10.0],
    "avatar": b"\x89PNG...",
    "tags": ("admin", "beta"),  # tuples come back as lists
}

data = btoon.encode(doc)
back = btoon.decode(data)
print(f"{len(data)} bytes; round trip ok: {back == {**doc, 'tags': list(doc['tags'])}}")

rows = [{"id": i, "city": "Tallinn", "temp": 20 + i / 10} for i in range(500)]
tabular = btoon.encode(rows)
print(f"500 rows: BTOON {len(tabular)} B, MessagePack-style {len(btoon.encode(rows, auto_tabular=False))} B, "
      f"JSON {len(json.dumps(rows))} B")
