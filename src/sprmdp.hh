
/*
 * SPRMDP - Simple Parallel Reliable Message Delivery Protocol.
 *
 */


#include <cstdint>
#include <list>
#include <vector>

#define SPRMDP_MSG_FLAG_SERVICE (0x80)
#define SPRMDP_MSG_FLAG_DELIVERED (0x40)
#define SPRMDP_MSG_FLAG_MULTIPART (0x20)
#define SPRMDP_MSG_FLAG_CANCELLED (0x10)
#define SPRMDP_MSG_FLAG_SERVICE_TYPE_MASK (0x0F)

#define SPRMDP_MSG_FLAG_SERVICE_CONFIRMATION (0x01)

#define SPRMDP_MSG_ID_MAX (0x003FFFFFFFFFFFFFul)
#define SPRMDP_MSG_ID_MASK (~(0xFFull << 56))

//TODO: Destination peer ID - ?

struct sprmdp_msg_header
{
    inline uint8_t get_flags() const { return flags_and_id >> 56; }
    inline uint64_t get_id() const { return flags_and_id & SPRMDP_MSG_ID_MASK; }
    inline void set_flags(uint8_t flags) { flags_and_id |= ((uint64_t)flags) << 56; }

    uint64_t flags_and_id;

    uint32_t msg_size_bytes;

    uint32_t multipart_msg_block_seq_num;

    uint32_t crc32;

    //const void* get_data() const { return ((const uint8_t*)this) + sizeof(*this); }

} __attribute__((packed));


struct sprmdp_msg_confirmation_error
{
    uint32_t multipart_msg_block_seq_num;
    uint32_t flags;
    uint32_t data; // crc32 or msg length
} __attribute__((packed));

struct sprmdp_msg_confirmation_header
{
    // sprmdp_msg_header header;

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

int sprmdp_init(uint16_t port, uint32_t block_size, size_t confirmation_max_wait_us = 1000);


// return peer ID
uint64_t sprmdp_connect(const char* host, uint16_t port);


//TODO: enqueue: void*, vector, string, iovec, ...
//TODO: get list of timeouted transmissions

// return msg ID
uint64_t sprmdp_enqueue(uint64_t peer_id, void* data, size_t length, size_t timeout_us = 0);

//TODO: recv: void* vector, string, iovec, ...

bool sprmdp_is_delivered(uint64_t peer_id, uint64_t msg_id);

// return list of messages
std::list<std::vector<uint8_t>> sprmdp_get_rcvd(uint64_t peer_id);

/*
TODO:
- get list of new peers
- get list of all peers
- reset peer
*/

void sprmdp_print_state();

