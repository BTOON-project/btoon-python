# btoon-python

pybind11 extension wrapping btoon-core. PyPI package `btoon`, BTOON spec 0.3.

## Commands

```bash
uv venv --python 3.13 .venv && uv pip install --python .venv ".[test]" numpy   # builds the extension
.venv/bin/pytest -q          # use the pytest entry point, NOT `python -m pytest` (cwd would shadow the install)
PYTHONDEVMODE=1 .venv/bin/pytest -q
# free-threaded: build into a 3.14t venv the same way, then
PYTHON_GIL=0 .venv-ft/bin/pytest -q
```

- Core comes from the `core/` git submodule. If that is empty, the build falls back to
  `../btoon-core`, or whatever `BTOON_CORE_DIR=...` points to. The tests read
  `core/tests/vectors/btoon-vectors.json`, falling back to `../btoon-core/...` or
  `BTOON_VECTORS`.
- Keep build output off the SMB share: `uv pip install -C build-dir=/tmp/... .`.
- Wheels: `.github/workflows/wheels.yml` (cibuildwheel), including cp314t/cp315t. Local
  Linux check: `uvx cibuildwheel@4.3.5 --platform linux` in a copy of the repo that has
  `core/` populated (`CIBW_BUILD="cp315-manylinux_aarch64 cp314t-manylinux_aarch64"` for
  a quick one).
- Crash-prone probes (fault injection with `_testcapi.set_nomemory`, sanitizers): use a
  uv-managed or pyenv Python, not Homebrew's framework Python, which pops a macOS crash
  dialog. uv's Pythons lack `_testcapi`; pyenv's have it.

## Layout

- `src/btoon_python.cpp`:
  - Encode: Python objects → `btoon::Encoder` directly. `classify()` decides what an object
    is, both for encoding and for whether `default()` must be asked. `PyEncoder` mirrors
    core's tabular qualification and size rule (including rows that `default()` turns into
    dicts) and uses `beginTabular()`/`tabular_beneficial()`.
  - Decode: core `decode()` (GIL released) → `btoon::Value` → Python. `to_py()` frees each
    part of the tree once converted. Don't call `malloc_trim` or similar: a library must
    not change allocator state for the whole host process.
- `btoon/__init__.py`: the public API and option handling. It calls `_btoon` positionally.
  `btoon/_types.py`: `BtoonError`, `BtoonTypeError`, `ExtType`, `Timestamp`, `Date`,
  `DateTime`.
- `tests/`: `test_vectors.py` (shared conformance vectors, including decode → encode
  round trips), `test_api.py`, `test_types.py` (`Date`/`DateTime`), `test_threads.py`
  (free-threading stress), `test_robustness.py` (fuzz, leaks, refcounts, allocation
  failures, peak memory).

## Rules

- Hostile bytes are only ever parsed by core's decoder. Don't add parsing to the binding.
- When converting, hold strong references to anything user code could free: `default()`,
  `tzinfo.utcoffset()` and numpy attributes can all run arbitrary Python. That's why lists
  and dicts are snapshotted.
- The module declares `py::mod_gil_not_used()`, so the code must be safe without a GIL:
  - never hold a borrowed reference into a list or dict another thread can change; copy
    lists with `PySequence_Tuple` and dicts with `PyDict_Next` under `CriticalSection`
    (no Python code inside it);
  - module state (`Cache`) is written once in module init and read-only afterwards;
  - check every CPython call that can fail (`steal()` throws the pending error), so an
    allocation failure is a `MemoryError`, not a crash.
- Canonical output must match the vectors. Any encoding change has to pass
  `test_canonical_encode` and `test_decode_then_encode_is_lossless`.
