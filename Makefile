BINDIR = ./bin
TARGET = $(BINDIR)/toy_system

CC = gcc
CFLAGS = -Wall -O -g -Iui -Isystem -Iweb_server

OBJS = main.o ./system/system_server.o ./ui/gui.o ./web_server/web_server.o

.PHONY: clean

all: $(TARGET)
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<
clean:
	rm -rf $(OBJS)
