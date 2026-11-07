# Convenience targets. Uses uv (https://docs.astral.sh/uv/).
PY ?= 3.13
VENV := .venv

.PHONY: dev test bench wheel sdist clean

dev:
	uv venv --python $(PY) $(VENV)
	uv pip install --python $(VENV) ".[test]" numpy

test: dev
	$(VENV)/bin/pytest -q

wheel:
	uv build --wheel

sdist:
	uv build --sdist

clean:
	rm -rf build dist $(VENV) .pytest_cache
	find . -name __pycache__ -type d -prune -exec rm -rf {} +
