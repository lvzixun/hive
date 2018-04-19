# CFLAGS:= -g -Wall -DDEBUG_MEMORY -std=gnu99  -Isrc/
CFLAGS:= -g -Wall -O2 -Isrc/ -std=gnu99

CC:= cc

SOURCE_C := src/hive.c src/hive_actor.c src/hive_memory.c \
	src/hive_mq.c src/hive_log.c src/socket_mgr.c \
	src/hive_bootstrap.c src/actor_agent_gate.c src/actor_log.c \
	src/lhive_buffer.c  src/hive_timer.c

SOURCE_O := $(SOURCE_C:.c=.o)


all: hive

hive: $(SOURCE_O)
	$(CC) -o $@ $^ -llua -lpthread -lm -ldl


clean:
	rm -rf $(SOURCE_O)


.PHONY: all clean
