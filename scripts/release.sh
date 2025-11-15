#!/bin/bash
# Build and prepare BTOON Python library for release

set -e

echo "Building BTOON Python Library for Release"
echo "========================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check Python version
PYTHON_VERSION=$(python3 --version)
echo "Python version: $PYTHON_VERSION"

# Clean previous builds
echo -e "\n${YELLOW}Cleaning previous builds...${NC}"
rm -rf build/ dist/ *.egg-info/
find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
find . -type f -name "*.pyc" -delete 2>/dev/null || true
find . -type f -name "*.so" -delete 2>/dev/null || true

# Build the core library if needed
if [ ! -f "../btoon-core/build/libbtoon_core.a" ]; then
    echo -e "\n${YELLOW}Building btoon-core library...${NC}"
    cd ../btoon-core
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
    cd ../../btoon-python
fi

# Install build dependencies
echo -e "\n${YELLOW}Installing build dependencies...${NC}"
python3 -m pip install --upgrade pip setuptools wheel build
python3 -m pip install pybind11

# Build the extension module
echo -e "\n${YELLOW}Building extension module...${NC}"
python3 setup.py build_ext --inplace

# Run tests
echo -e "\n${YELLOW}Running tests...${NC}"
python3 test_quick.py

# Run examples
echo -e "\n${YELLOW}Running examples...${NC}"
for example in examples/*.py; do
    echo "Running $example..."
    python3 "$example" > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo -e "  ${GREEN}✓${NC} $(basename $example)"
    else
        echo -e "  ${RED}✗${NC} $(basename $example)"
    fi
done

# Build distribution packages
echo -e "\n${YELLOW}Building distribution packages...${NC}"

# Build source distribution
python3 -m build --sdist

# Build wheel for current platform
python3 -m build --wheel

# List created packages
echo -e "\n${GREEN}✅ Release build completed successfully!${NC}"
echo "Packages created:"
ls -lh dist/

echo ""
echo "To test installation locally:"
echo "  pip install dist/btoon-*.whl"
echo ""
echo "To upload to TestPyPI (for testing):"
echo "  python3 -m twine upload --repository testpypi dist/*"
echo ""
echo "To upload to PyPI (for production):"
echo "  python3 -m twine upload dist/*"
echo ""
echo "Note: You'll need to configure ~/.pypirc with your API tokens"
