CC = gcc
CFLAGS = -Wall -Wextra -I..

TESTS = test_kstring

all: $(TESTS)

test_kstring: test_kstring.c ../kstring.c
	$(CC) $(CFLAGS) $< -o $@

run: $(TESTS)
	./test_kstring

clean:
	rm -f $(TESTS)

.PHONY: all run clean
