# Releasing btoon (PyPI)

1. Make sure `core/` points at a tagged btoon-core commit:
   `git -C core checkout vX.Y.Z && git add core`.
2. Bump `version` in `pyproject.toml` and `__version__` in `btoon/__init__.py`, and update
   `CHANGELOG.md`.
3. Push a tag `vX.Y.Z`. `.github/workflows/wheels.yml` builds wheels on Linux (glibc and
   musl, x86_64 and aarch64), macOS (arm64 and x86_64) and Windows, plus the sdist. It
   then publishes to PyPI through trusted publishing.
   - One-time setup: on PyPI, add a trusted publisher for `BTOON-project/btoon-python`,
     workflow `wheels.yml`, environment `pypi`.
4. Smoke-test with `pip install btoon==X.Y.Z && python -c "import btoon; print(btoon.__version__)"`.
