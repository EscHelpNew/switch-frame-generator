# Use a native Windows Python; override PYTHON if the shell finds MSYS Python.
PYTHON ?= py -3
.PHONY: all test host
all:
	$(PYTHON) build.py
test:
	$(PYTHON) build.py --test
host:
	$(PYTHON) build.py --host-only
