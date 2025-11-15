# BTOON Python Release Guide

## Prerequisites

1. **Build Tools**
   - Python >= 3.7
   - C++ compiler (gcc/clang/MSVC)
   - CMake
   - pybind11

2. **PyPI Account**
   - Register at https://pypi.org
   - Generate API token
   - Configure `~/.pypirc`:
   ```ini
   [distutils]
   index-servers =
     pypi
     testpypi

   [pypi]
   repository = https://upload.pypi.org/legacy/
   username = __token__
   password = <your-pypi-token>

   [testpypi]
   repository = https://test.pypi.org/legacy/
   username = __token__
   password = <your-testpypi-token>
   ```

3. **Build Dependencies**
   ```bash
   pip install --upgrade pip setuptools wheel build twine
   ```

## Release Process

### 1. Prepare Release

```bash
# Update version in setup.py and pyproject.toml
# Edit both files to increment version

# Clean previous builds
make clean

# Build and test
make build
make test
```

### 2. Build Distribution Packages

```bash
# Using script
./scripts/release.sh

# Or manually:
python -m build  # Creates both wheel and sdist

# Or using Makefile
make dist
```

### 3. Test Package Locally

```bash
# Create test environment
python -m venv test_env
source test_env/bin/activate  # On Windows: test_env\Scripts\activate

# Install from wheel
pip install dist/btoon-*.whl

# Test
python -c "import btoon; print(btoon.encode({'test': True}))"

# Clean up
deactivate
rm -rf test_env
```

### 4. Upload to TestPyPI (Optional)

```bash
# Upload to test repository
python -m twine upload --repository testpypi dist/*

# Test installation from TestPyPI
pip install --index-url https://test.pypi.org/simple/ btoon
```

### 5. Publish to PyPI

```bash
# Upload to PyPI
python -m twine upload dist/*

# Or with Makefile
make publish
```

### 6. Verify Publication

```bash
# Check on PyPI
pip search btoon  # Note: may be disabled

# Install from PyPI
pip install btoon

# Verify
python -c "import btoon; print(btoon.__version__)"
```

## Automated Release (CI/CD)

The GitHub Actions workflow (`.github/workflows/publish.yml`) automates:
- Building wheels for multiple Python versions
- Building for multiple platforms
- Running tests
- Publishing to PyPI

To trigger:
1. Create a GitHub release
2. The workflow will automatically build and publish

## Platform Support

### Binary Wheels (Fast)
- Linux x64, ARM64 (manylinux)
- macOS x64, ARM64 (universal2)
- Windows x64, x86

### Source Distribution (Requires Compilation)
- Any platform with C++ compiler

## Building Wheels for Multiple Platforms

### Using cibuildwheel (Recommended)

```bash
pip install cibuildwheel

# Build wheels for current platform
cibuildwheel --platform auto

# Build for all platforms (CI only)
cibuildwheel --platform all
```

### Manual Platform Builds

```bash
# Linux (manylinux)
docker run -v $(pwd):/io quay.io/pypa/manylinux2014_x86_64 \
  /io/scripts/build_manylinux.sh

# macOS Universal
python setup.py bdist_wheel --plat-name macosx_10_9_universal2

# Windows
python setup.py bdist_wheel --plat-name win_amd64
```

## Version Management

Follow semantic versioning:
- **Patch** (0.0.x): Bug fixes, minor changes
- **Minor** (0.x.0): New features, backward compatible
- **Major** (x.0.0): Breaking changes

Update version in:
- `setup.py` (__version__)
- `pyproject.toml` (version)
- `btoon/__init__.py` (__version__)

## Troubleshooting

### Build Errors
- Ensure btoon-core is built with `-fPIC`
- Check pybind11 installation
- Verify CMake version

### Upload Issues
- Check PyPI token configuration
- Verify package name availability
- Ensure unique version number

### Import Errors
- Check Python version compatibility
- Verify C++ runtime libraries
- Test in clean environment

## Testing Matrix

Test on multiple Python versions:
```bash
# Using tox
pip install tox
tox

# Manual testing
for ver in 3.7 3.8 3.9 3.10 3.11 3.12; do
    python$ver -m venv test_env_$ver
    test_env_$ver/bin/pip install dist/*.whl
    test_env_$ver/bin/python -c "import btoon; print(f'Python {ver}: OK')"
done
```

## Checklist

Before release:
- [ ] Tests pass on all Python versions
- [ ] Examples work
- [ ] Type stubs updated
- [ ] Documentation updated
- [ ] CHANGELOG updated
- [ ] Version incremented in all files
- [ ] Build succeeds on all platforms
- [ ] Local install test passes
- [ ] TestPyPI upload successful (optional)
