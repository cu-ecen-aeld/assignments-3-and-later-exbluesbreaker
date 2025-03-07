CC := $(if $(CROSS_COMPILE),$(CROSS_COMPILE)gcc,gcc)
CFLAGS = -Wall -Wextra -O2
TARGET = writer
SRC = writer.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
