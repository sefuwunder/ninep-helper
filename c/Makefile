CC = gcc
# -Wno-main: Plan 9 main returns void and exits via exits(); it
# never falls off the end, so gcc's int-main expectation is moot.
CFLAGS = -std=gnu99 -Wall -Wextra -Wno-main -O2

OBJS = compat.o ninepc.o route.o man.o

all: helper helper_test

helper: helper.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ helper.o $(OBJS)

helper_test: test.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ test.o $(OBJS)

test: helper helper_test
	sh tests/run.sh

clean:
	rm -f *.o helper helper_test
