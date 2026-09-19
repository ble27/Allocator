# Variables for compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -lpthread -Wno-deprecated-declarations
TARGET = allocator

# Default rule: Compiles the binary
all: $(TARGET)

$(TARGET): allocator.c
	$(CC) -o $(TARGET) allocator.c $(CFLAGS)

# Shortcut rule to compile and execute immediately
run: $(TARGET)
	./$(TARGET)

# Clean rule to wipe compiled binary artifacts
clean:
	rm -f $(TARGET)
