# Makefile for BTOON Python library

.PHONY: all build clean test install release publish help dev

# Python command
PYTHON := python3

# Default target
all: build

# Build the extension module
build:
	@echo "Building BTOON Python module..."
	@$(PYTHON) setup.py build_ext --inplace

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf build/ dist/ *.egg-info/
	@find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
	@find . -type f -name "*.pyc" -delete 2>/dev/null || true
	@find . -type f -name "*.so" -delete 2>/dev/null || true
	@find . -type f -name "*.pyd" -delete 2>/dev/null || true
	@rm -rf .pytest_cache .coverage htmlcov/

# Install dependencies
install:
	@echo "Installing dependencies..."
	@$(PYTHON) -m pip install --upgrade pip setuptools wheel
	@$(PYTHON) -m pip install pybind11
	@$(PYTHON) -m pip install -e .

# Install development dependencies
install-dev:
	@echo "Installing development dependencies..."
	@$(PYTHON) -m pip install -e ".[dev]"

# Run tests
test: build
	@echo "Running tests..."
	@$(PYTHON) test_quick.py

# Run full test suite
test-full: build
	@echo "Running full test suite..."
	@$(PYTHON) -m pytest tests/ -v

# Run examples
examples: build
	@echo "Running examples..."
	@for f in examples/*.py; do \
		echo "Running $$f..."; \
		$(PYTHON) "$$f" > /dev/null && echo "  ✓ $$(basename $$f)" || echo "  ✗ $$(basename $$f)"; \
	done

# Build distribution packages
dist: clean build test
	@echo "Building distribution packages..."
	@$(PYTHON) -m pip install --upgrade build
	@$(PYTHON) -m build
	@ls -lh dist/

# Build wheel only
wheel: clean build
	@echo "Building wheel..."
	@$(PYTHON) -m build --wheel
	@ls -lh dist/*.whl

# Build source distribution only
sdist: clean
	@echo "Building source distribution..."
	@$(PYTHON) -m build --sdist
	@ls -lh dist/*.tar.gz

# Test installation
test-install: dist
	@echo "Testing installation..."
	@$(PYTHON) -m venv test_env
	@./test_env/bin/pip install dist/*.whl
	@./test_env/bin/python -c "import btoon; print(btoon.encode({'test': True}))"
	@rm -rf test_env

# Upload to TestPyPI
publish-test: dist
	@echo "Publishing to TestPyPI..."
	@$(PYTHON) -m pip install --upgrade twine
	@$(PYTHON) -m twine upload --repository testpypi dist/*

# Upload to PyPI
publish: dist
	@echo "Publishing to PyPI..."
	@$(PYTHON) -m pip install --upgrade twine
	@$(PYTHON) -m twine upload dist/*

# Development mode
dev:
	@echo "Installing in development mode..."
	@$(PYTHON) -m pip install -e ".[dev,full]"
	@$(PYTHON) setup.py develop

# Code formatting
format:
	@echo "Formatting code..."
	@$(PYTHON) -m pip install black isort
	@$(PYTHON) -m black btoon/ examples/ tests/
	@$(PYTHON) -m isort btoon/ examples/ tests/

# Linting
lint:
	@echo "Linting code..."
	@$(PYTHON) -m pip install flake8 mypy
	@$(PYTHON) -m flake8 btoon/ examples/
	@$(PYTHON) -m mypy btoon/

# Type checking
typecheck:
	@echo "Type checking..."
	@$(PYTHON) -m pip install mypy
	@$(PYTHON) -m mypy btoon/ --ignore-missing-imports

# Coverage report
coverage: build
	@echo "Running coverage..."
	@$(PYTHON) -m pip install pytest-cov
	@$(PYTHON) -m pytest tests/ --cov=btoon --cov-report=html --cov-report=term
	@echo "Coverage report generated in htmlcov/"

# Benchmark
benchmark: build
	@echo "Running benchmarks..."
	@$(PYTHON) -m pip install pytest-benchmark
	@$(PYTHON) -c "import timeit, btoon, json; \
		data = {'test': list(range(1000))}; \
		bt = timeit.timeit(lambda: btoon.encode(data), number=1000); \
		js = timeit.timeit(lambda: json.dumps(data), number=1000); \
		print(f'BTOON: {bt:.3f}s, JSON: {js:.3f}s, Speedup: {js/bt:.1f}x')"

# Help target
help:
	@echo "BTOON Python Library Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  make build        - Build the extension module"
	@echo "  make clean        - Clean build artifacts"
	@echo "  make install      - Install the package"
	@echo "  make install-dev  - Install with dev dependencies"
	@echo "  make test         - Run quick tests"
	@echo "  make test-full    - Run full test suite"
	@echo "  make examples     - Run examples"
	@echo "  make dist         - Build distribution packages"
	@echo "  make wheel        - Build wheel only"
	@echo "  make sdist        - Build source distribution only"
	@echo "  make test-install - Test installation"
	@echo "  make publish-test - Publish to TestPyPI"
	@echo "  make publish      - Publish to PyPI"
	@echo "  make dev          - Install in development mode"
	@echo "  make format       - Format code with black"
	@echo "  make lint         - Check code style"
	@echo "  make typecheck    - Run type checking"
	@echo "  make coverage     - Generate coverage report"
	@echo "  make benchmark    - Run benchmarks"
	@echo "  make help         - Show this help"
