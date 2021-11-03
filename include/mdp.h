#pragma once

/*
 * SPRMDP - Simple Parallel Reliable Message Delivery Protocol.
 *
 */


#include <cstdint>
#include <list>
#include <vector>

//TODO: Destination peer ID - ?


/*
  1-st step:
    - w/o multipart messages
    - w/o retransmit and timeouts
    - w/o settings negotiation (defaults)
    - w/o

*/

//TODO: multiple ports per instance
//TODO: multiple instances per process

//TODO: mdp_config, from dict, from string
//TODO: mdp_stats, to dict, to string


void mdp_start_receiver(const char* listen_addr, uint16_t listen_port);
void mdp_start_sender(const char* dest_addr, uint16_t dest_port);

// return peer ID
uint64_t mdp_connect(const char* host, uint16_t port);


//TODO: enqueue: void*, vector, string, iovec, ...
//TODO: get list of timeouted transmissions

// return msg ID
uint64_t mdp_enqueue(uint64_t peer_id, void* data, size_t length, size_t timeout_us = 0);

//TODO: recv: void* vector, string, iovec, ...

bool mdp_is_delivered(uint64_t peer_id, uint64_t msg_id);

// return list of messages
std::list<std::vector<uint8_t>> mdp_get_rcvd(uint64_t peer_id);

/*
TODO:
- get list of new peers
- get list of all peers
- reset peer
*/

void mdp_print_state();

