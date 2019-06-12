#ifndef __MDP_HH__
#define __MDP_HH__

/*
 * SPRMDP - Simple Parallel Reliable Message Delivery Protocol.
 *
 */


#include <cstdint>
#include <list>
#include <vector>

#define MDP_MSG_FLAG_SERVICE (0x80)
#define MDP_MSG_FLAG_DELIVERED (0x40)
#define MDP_MSG_FLAG_MULTIPART (0x20)
#define MDP_MSG_FLAG_CANCELLED (0x10)
#define MDP_MSG_FLAG_SERVICE_TYPE_MASK (0x0F)

#define MDP_MSG_FLAG_SERVICE_CONFIRMATION (0x01)

#define MDP_MSG_ID_MAX (0x003FFFFFFFFFFFFFul)
#define MDP_MSG_ID_MASK (~(0xFFull << 56))

#define MDP_MSG_FLAGS(X) (((uint64_t)(X)) << 56)

//TODO: Destination peer ID - ?

struct mdp_msg_header
{
    inline uint8_t get_flags() const { return flags_and_id >> 56; }
    inline uint64_t get_id() const { return flags_and_id & MDP_MSG_ID_MASK; }
    inline void set_flags(uint8_t flags) { flags_and_id |= ((uint64_t)flags) << 56; }

    uint64_t flags_and_id;

    uint32_t msg_size_bytes;

    uint32_t multipart_msg_block_seq_num;

    uint32_t crc32;

    //const void* get_data() const { return ((const uint8_t*)this) + sizeof(*this); }

} __attribute__((packed));


struct mdp_msg_confirmation_error
{
    uint32_t multipart_msg_block_seq_num;
    uint32_t flags;
    uint32_t data; // crc32 or msg length
} __attribute__((packed));

struct mdp_msg_confirmation_header
{
    // mdp_msg_header header;

    uint64_t complete_confirmed_msg_id; // non-decreasing sequence of delivered msg ID - confirmes confirmation received



    // optional confirmation bitmap

    // optional error list

} __attribute__((packed));

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

#endif
