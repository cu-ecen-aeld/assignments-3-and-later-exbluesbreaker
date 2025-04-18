ifndef CC
CC := $(if $(CROSS_COMPILE),$(CROSS_COMPILE)gcc,gcc)
endif
CFLAGS = -Wall -Wextra -Werror -O2
TARGET = aesdsocket
SRC = aesdsocket.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

default: all

.PHONY: all clean
