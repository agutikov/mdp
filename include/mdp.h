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



void mdp_print_state();

