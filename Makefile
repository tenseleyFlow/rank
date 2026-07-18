include config.mk

SRC = \
	src/main.c \
	src/options.c \
	src/plan.c \
	src/line.c \
	src/key.c \
	src/radix.c \
	src/sort.c \
	src/cmp.c \
	src/numeric.c \
	src/locale.c \
	src/merge.c \
	src/external.c \
	src/check.c \
	src/output.c \
	src/util.c \
	src/sys/file.c \
	src/sys/cpu.c

OBJ = $(SRC:.c=.o)
DEP = $(OBJ:.o=.d)

CPPFLAGS += -I. -Isrc -D_DEFAULT_SOURCE
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wconversion
LDFLAGS ?=

.PHONY: all check unit golden perf-smoke sanitize clean distclean ref-sort

all: rank

config.mk config.h: configure
	./configure

rank: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c config.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

check: unit golden perf-smoke

unit: rank
	sh tests/unit/run.sh

golden: rank
	sh tests/golden/run.sh

perf-smoke: rank
	sh bench/run-smoke.sh

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='$(CFLAGS) $(SANITIZE_FLAGS)' LDFLAGS='$(LDFLAGS) $(SANITIZE_FLAGS)' check

ref-sort:
	sh scripts/build-gnu-sort.sh

clean:
	rm -f rank $(OBJ) $(DEP)

distclean: clean
	rm -f config.h config.mk
	rm -rf build

-include $(DEP)
