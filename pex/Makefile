CC=gcc
TARGET = pe_exchange
TEST_TARGET = tests/unit-tests
CFLAGS= -Wall -Werror -Wvla -O0 -std=c11 -g -fsanitize=address,leak
LDFLAGS=-lm
BINARIES=pe_trader pe_exchange

all: $(BINARIES)

pe_trader: pe_trader.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

pe_exchange: pe_exchange.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.SUFFIXES: .c .o

.c.o:
	 $(CC) $(CFLAGS) -c $<

.PHONY: tests
tests:
	gcc -DTESTING tests/unit-tests.c pe_exchange.c -o $(TEST_TARGET) tests/libcmocka-static.a -lm


run_tests:
	./$(TEST_TARGET);

.PHONY: clean
clean:
	rm -f *.o *.obj $(BINARIES) $(TEST_TARGET)
