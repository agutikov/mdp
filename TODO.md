


https://think-async.com/Asio/asio-1.18.1/doc/asio/overview/networking/bsd_sockets.html



TODO: cleanup existing code, remove static vars, use C++ containers and simple locks

TODO: in-code TODOs


TODO: Temporary simple replacement for buffer

  Later will make iovec-based buffer sequence.



TODO: received and delivered message state

  Message can be received - then confirmation goes to the peer and it will not retransmit the message.
  Message can be delivered (to the application level) - then the other application level can get the confirmation.

  Main difference - receive confirmation is per packet, while delivery confirmation is per message.




TODO: library interface, resources, stats, config(timeouts, queues, ...)

Interface:
- send
  - number of msgs and peers
    - one msg to one peer
    - one msg to multiple peers
    - multiple msgs to one peer
    - multiple msgs each to different peers
    - multiple msgs each to multiple peers
  - need to confirm delivered to sender - on application level
  - need to confirm receive to sender - on application level
  - timeout
    - waiting for receive confirmation == timeout of all retransmissions
    - waiting for delivery confirmation

- peers
  - connect peer - pin peer in the list, it will not be dropped after timeout
  - disconnect peer - remove from pinned list, but peer may still be active
  - blacklist peer - drop msgs from it
  - get peers
    - get list of active peers
    - get list of connected peers with states (active / inactive)
    - get list of all known peers (active + connected)
  - get active peers change (after last get list of active peers)
  - get some peer stats
  - get active peers stats

- receive
  - get received messages
  - get confirmations and timeouts == sended messages update
    - message was received by peer
    - message was delivered by peer to the application
  - get message state by id
  - get messages states (by id list)
  - get all messages states by peer


Config:
- timings
  - peer_start_heartbeat_timeout - after last packet from peer, until start heartbeat
  - peer_heartbeet_delay - between heartbeats
  - peer_heartbeat_timeout - after last heartbeat before peer been considered inactive
  - max_confirmation_delay, max_confirmation_frequency - after last non-confirmed message received, until sending the confirmation
  - packet_retransmission_timeout
- format:
  - max_message_size
  - min_packet_size
  - max_packet_size
- resources
  - max_enqueued_messages
  - max_enqueued_packets


TODO: crc

TODO: multi-packet messages


TODO: iovec-based buffer sequence
  Eliminate memory copy - use refcounted iovecs.
  Custom allocator for memory blocks.
  For example - simple allocator with all blocks of max packet size.
  With free list of iovecs.



TODO: threads
  total or per endpoint
  - single thread
  - two thread: sender and receiver
  - multiple threads: sender, receiver, send preparer, received handler, etc...
  - dynamic number of threads - ???
  - asyncronous implementation options:
    - asio
    - coroutines
    - seastar
    - boost fiber - network???
    - event loops: libevent, libuv, libev, libuev, https://cpp.libhunt.com/libs/asynchronous-event-loop


TODO: performance
  lock-free queues
  trie as map
  preallocate all buffers, only move pointers


TODO: MTU auto-detect

TODO: description in English


======================================

IO performance

- libaio (io_submit, etc...)
- io_uring
- DPDK



======================================


Two ways of huge object/file transmission:
1. Writes with offset
   1. Head message creates file with size
   2. Ongoing messages writes blocks with different offsets
2. Sequence of appends
   1. First message creates file
   2. Ongoing messages have sequence number and data to append


Benchmarks:
- userspace block device
  - memory
  - file
- userspace flat filesystem (without directories)
  - in memory
  - in fs directory



======================================


+ moved flags, flags/id macroses, flags_and_id class


+ send complete_confirmed_msg_id with each packet, heartbeat only when no transmission



data retransmits


refactor, multiple files

library interface + test/banchmark


reconnect, peer_id, single map by addr+port+id
library initialize for ipv4 or ipv6
heartbeat timeout drop peer if no buffered data
disconnect


retransmits, confirmations, timeouts

complete algorythm implementation




======================================

?????????? flow control
?????????? request to retransmit (before confirmation timeout)

======================================

multithread handle rcvd msgs

multithread send


//TODO: multiple ports per instance
//TODO: multiple instances per process

//TODO: mdp_config, from dict, from string
//TODO: mdp_stats, to dict, to string



//TODO: Solve problen how to drop connection? When one side forget about peer then it re-creates it after receiving next heartbeat.



===========================

stats per peer
stats overall

===========================

benchmark

configurations:
- 1-to-1
  - request-response
  - uni-directional streaming
  - bi-directional streaming
- n-to-1
  - number of peers
- n-to-n



metrics:
- latency
  - delivery time (confirmation time)
  - roundtrip (request-response)
- throughput
  - messages per second
  - packets per second
  - bytes per second
  - payload data bytes per second
- load curve
  - latency per mps
  - latency per bps
- retries
  - message
    - number of affected, delayed msgs
  - packet
    - number of retries == number of dropped packets
    - number of retries of one packet (max, avg)
    - number of affected (delayed) packets







