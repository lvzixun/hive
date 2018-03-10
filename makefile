# CFLAGS:= -g -Wall -DDEBUG_MEMORY -Isrc/
CFLAGS:= -g -Wall -O2 -Isrc/
CC:= cc

SOURCE_C := src/hive.c src/hive_actor.c src/hive_memory.c \
	src/hive_mq.c src/hive_log.c src/socket_mgr.c \
	src/hive_bootstrap.c src/actor_agent_gate.c
SOURCE_O := $(SOURCE_C:.c=.o)


all: hive

hive: $(SOURCE_O)
	$(CC) -o $@ $^ -llua -lpthread -lm -ldl

clean:
	rm -rf $(SOURCE_O)


.PHONY: all clean