CFLAGS:= -g -Wall -DDEBUG_MEMORY -Isrc/

SOURCE_C := src/hive.c src/hive_actor.c src/hive_memory.c \
	src/hive_mq.c src/actor_bootstrap.c src/hive_log.c src/socket_mgr.c
SOURCE_O := $(SOURCE_C:.c=.o)


all: hive

hive: $(SOURCE_O)
	clang -o $@ $^ -llua -lpthread

clean:
	rm -rf $(SOURCE_O)


.PHONY: all clean