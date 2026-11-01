CC := $(shell if [ -f ./toolchain/usr/bin/gcc ]; then echo "./toolchain/usr/bin/gcc"; else echo "gcc"; fi)

TOOLCHAIN_DIR = ./toolchain
CFLAGS = -Wall -Wextra -O2 -Iinclude -pthread
LDFLAGS = -lm -lpthread -lmariadb

ifeq ($(CC),./toolchain/usr/bin/gcc)
    CFLAGS += -m64 -B/usr/libexec/gcc/x86_64-redhat-linux/15/ -B$(TOOLCHAIN_DIR)/usr/lib64 -I$(TOOLCHAIN_DIR)/usr/include
    LDFLAGS += -L$(TOOLCHAIN_DIR)/usr/lib64 -L/usr/lib64
endif

SRC_COMMON = src/models.c src/crypto_sha256.c src/auth.c src/fare_engine.c src/db_adapter.c src/train_service.c src/booking_service.c src/concurrency.c
OBJ_COMMON = $(SRC_COMMON:.c=.o)

all: train_system run_tests

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

train_system: $(OBJ_COMMON) src/main.o src/ui.o src/server.o src/client.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

run_tests: $(OBJ_COMMON) tests/test_suite.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: run_tests
	./run_tests

simulate: train_system
	./train_system --simulate

clean:
	rm -f src/*.o tests/*.o train_system run_tests

.PHONY: all test simulate clean
