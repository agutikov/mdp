#!/bin/sh
set +e

gcc -ljemalloc -O2 -lstdc++ -std=gnu++17 -lpthread peer.cc net.cc sprmdp.cc -o ./peer
