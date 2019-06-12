

CFLAGS = -ljemalloc -O2 -lstdc++ -std=gnu++17 -lpthread -pthread -static
#CFLAGS = -ljemalloc -O0 -lstdc++ -std=gnu++17 -lpthread -pthread -ggdb

.PHONY: all
all: peer

.PHONY: clean
clean:
	rm -f peer

.PHONY: rebuild
rebuild: clean all

peer: peer.cc mdp.cc net.cc
	g++ $(CFLAGS) $^ -o $@




