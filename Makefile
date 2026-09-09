CC ?= gcc
CFLAGS ?= -O2
WARNINGS = -Wall -Wextra

all: transform convert

transform: figproc_transform

convert: figproc_convert

figproc_transform: figproc.c
	$(CC) $(CFLAGS) $(WARNINGS) -DTRANSFORM -o $@ $<

figproc_convert: figproc.c
	$(CC) $(CFLAGS) $(WARNINGS) -DCONVERT -o $@ $<

test: all
	python3 tests/run_tests.py

clean:
	rm -f figproc_transform figproc_convert

.PHONY: all transform convert test clean
