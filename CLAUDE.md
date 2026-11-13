# btoon-python

pybind11 extension wrapping btoon-core. PyPI package `btoon`, BTOON spec 0.2.

## Commands

```bash
uv venv --python 3.13 .venv && uv pip install --python .venv ".[test]" numpy   # builds the extension
.venv/bin/pytest -q          # use the pytest entry point, NOT `python -m pytest` (cwd would shadow the install)
PYTHONDEVMODE=1 .venv/bin/pytest -q
```

- Core comes from the `core/` git submodule. If that is empty, the build falls back to
  `../btoon-core`, or whatever `BTOON_CORE_DIR=...` points to. The tests read
  `core/tests/vectors/btoon-vectors.json`, falling back to `../btoon-core/...` or
  `BTOON_VECTORS`.
- Wheels: `.github/workflows/wheels.yml` (cibuildwheel). Local Linux check:
  `uvx cibuildwheel@4.3.5 --platform linux` in a copy of the repo that has `core/`
  populated.

## Layout

- `src/btoon_python.cpp`:
  - Encode: Python objects → `btoon::Encoder` directly. `PyEncoder` mirrors core's
    tabular qualification and size rule and uses `beginTabular()`/`tabular_beneficial()`.
  - Decode: core `decode()` (GIL released) → `btoon::Value` → Python.
- `btoon/__init__.py`: the public API and option handling. `btoon/_types.py`:
  `BtoonError`, `BtoonTypeError`, `ExtType`, `Timestamp`.
- `tests/`: `test_vectors.py` (shared conformance vectors), `test_api.py`,
  `test_robustness.py` (fuzz, leaks, refcounts).

## Rules

- Hostile bytes are only ever parsed by core's decoder. Don't add parsing to the binding.
- When converting, hold strong references to anything user code could free: `default()`,
  `tzinfo.utcoffset()` and numpy attributes can all run arbitrary Python. That's why lists
  and dicts are snapshotted.
- Canonical output must match the vectors. Any encoding change has to pass
  `test_canonical_encode`.
