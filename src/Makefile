

CFLAGS = -ljemalloc -O2 -lstdc++ -std=gnu++17 -lpthread -pthread
#CFLAGS = -ljemalloc -O0 -lstdc++ -std=gnu++17 -lpthread -pthread -ggdb

.PHONY: all
all: peer

.PHONY: clean
clean:
	rm -f peer

.PHONY: remake
remake: clean all

peer: peer.cc mdp.cc net.cc
	g++ $(CFLAGS) $^ -o $@




