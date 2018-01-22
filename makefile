CFLAGS:= -g -Wall -DDEBUG_MEMORY

SOURCE_C := hive.c hive_actor.c hive_memory.c hive_mq.c
SOURCE_O := $(SOURCE_C:.c=.o)


all: hive

hive: $(SOURCE_O)
	clang -o $@ $^ -llua -lpthread

clean:
	rm -rf $(SOURCE_O)


.PHONY: all clean