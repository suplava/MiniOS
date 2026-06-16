CC = gcc
CFLAGS = -Iinclude -Wall -Wextra -std=c11
TARGET = minios

SRC = kernel/main.c \
      kernel/console.c \
      kernel/trap.c \
      kernel/timer.c \
      kernel/syscall.c \
      kernel/memory.c \
      kernel/process.c \
      kernel/ramfs.c \
      kernel/shell.c \
      kernel/test.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) $(CFLAGS) -o $(TARGET)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe

