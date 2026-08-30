CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2
TARGET  = scheduler
SRC     = src/scheduler.c

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

# Run every case in tests/ and diff against the expected output
test: $(TARGET)
	@./run_tests.sh

clean:
	rm -f $(TARGET) tests/*.actual
