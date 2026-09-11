CFLAGS = -fsanitize=address,undefined -std=c99 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lm

SOURCES := $(wildcard *.c)
TARGETS := $(SOURCES:.c=)

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)
