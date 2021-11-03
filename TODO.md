

TODO: build, library, peer test app

TODO: library interface, resources, stats, config(timeouts, queues, ...)

TODO: crc

TODO: multi-packet messages

TODO: MTU auto-detect

TODO: description in English


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

lock-free queues
trie as map
preallocate all buffers, only move pointers

======================================


mustipart messages


======================================

?????????? flow control
?????????? request to retransmit (before confirmation timeout)

======================================

multithread handle rcvd msgs

multithread send


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







