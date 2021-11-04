#pragma once

/*
 * MDP - Message Delivery Protocol.
 *
 */

#include "asio/ip/udp.hpp"
#include <cstdint>
#include <list>
#include <vector>

using asio::ip::udp;

using mdp_endpoint_t = udp::endpoint;


std::string to_string(const mdp_endpoint_t& ep);

void mdp_start_receiver(const mdp_endpoint_t& ep);

void mdp_start_sender(const mdp_endpoint_t& ep);
void mdp_start_sender();


extern asio::io_context io_context;


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

