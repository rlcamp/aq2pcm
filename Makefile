# if you ever see a segfault or other unexpected nondeterminism in this (or any other) code,
# the immediate muscle memory response should be to recompile with the following, and then
# rerun the failing use case:
# make clean && make CFLAGS="-Og -g -fno-inline -fsanitize=address,undefined"

# overrideable vars used in implicit make rules

CFLAGS ?= -Os
CPPFLAGS += -Wall -Wextra -Wshadow -Wmissing-prototypes
LDFLAGS += ${CFLAGS}
LDLIBS ?= -framework AudioToolbox

TARGETS=aq2pcm

all : ${TARGETS}

aq2pcm : aq2pcm.o

*.o : Makefile

clean :
	$(RM) -rf *.o *.dSYM ${TARGETS}
.PHONY: clean all
