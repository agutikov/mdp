
#include "mdp.hh"

#include <map>
#include <memory>
#include <chrono>
#include <thread>
#include <string>
#include <iostream>
#include <cstring>
#include <mutex>
#include <atomic>
#include <algorithm>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "net.h"

using namespace std::chrono_literals;

struct mdp_transmission
{
    uint64_t id;

    mdp_msg_header header;

    std::vector<uint8_t> data;

    int retransmit_count = 0;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point next_restransmit;
};

struct mdp_confirmation
{
    uint64_t flags_and_id;

    int retransmit_count = 0;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point next_restransmit;
};

struct mdp_peer
{ //TODO: receiver and sender
    uint64_t id;
    net_addr addr;

    uint64_t last_sended_msg_id = 0;
    uint64_t complete_confirmed_msg_id = 0;

    std::chrono::steady_clock::time_point last_heartbeat_sent = std::chrono::steady_clock::now();

    int client_socket_fd = -1;

    // map of started transmissions with confirmation waiters
    std::map<uint64_t, mdp_transmission> transmissions;

    // map of receiving multipart messages
    // not implemented

    // map of confirmations, waiting for trimming by peer's complete_confirmed_msg_id
    std::map<uint64_t, mdp_confirmation> confirmations;
    std::mutex confirmations_mutex;

    // list of received messages, sinle-packet messages goes directly here
    std::list<std::vector<uint8_t>> rcvd;
};


struct mdp_iovecs
{
    iovec header_iovec;
    iovec payload_iovec;
} __attribute__((packed));

std::atomic<uint64_t> last_peer_id = 0;

std::map<uint64_t, std::shared_ptr<mdp_peer>> peer_by_id;
std::map<net_addr, std::shared_ptr<mdp_peer>> peer_by_addr;
std::mutex peer_mutex;

std::vector<std::shared_ptr<mdp_peer>> get_all_peers()
{
    std::vector<std::shared_ptr<mdp_peer>> all_peers;

    std::lock_guard<std::mutex> lock(peer_mutex);
    std::transform(peer_by_addr.begin(), peer_by_addr.end(), std::back_inserter(all_peers), [](auto p){return p.second;});

    return std::move(all_peers);
}

std::shared_ptr<mdp_peer> get_peer_by_id(uint64_t id)
{
    std::lock_guard<std::mutex> lock(peer_mutex);

    auto peer_it = peer_by_id.find(id);
    if (peer_it == peer_by_id.end()) {
        return nullptr;
    } else {
        return peer_it->second;
    }
}

std::shared_ptr<mdp_peer> get_peer_by_addr(const net_addr& addr)
{
    std::lock_guard<std::mutex> lock(peer_mutex);

    auto peer_it = peer_by_addr.find(addr);
    if (peer_it == peer_by_addr.end()) {
        return nullptr;
    } else {
        return peer_it->second;
    }
}

std::shared_ptr<mdp_peer> new_peer(const net_addr& addr)
{
    printf("new peer %s\n", addr_to_str(&addr));
    auto peer = std::make_shared<mdp_peer>();
    peer->id = last_peer_id.fetch_add(1);
    peer->addr = addr;

    {
        std::lock_guard<std::mutex> lock(peer_mutex);

        peer_by_addr.emplace(std::make_pair(peer->addr, peer));
        peer_by_id.emplace(std::make_pair(peer->id, peer));
    }

    return peer;
}

std::shared_ptr<mdp_peer> get_or_create_peer(const net_addr& addr)
{
    auto peer = get_peer_by_addr(addr);
    //printf("get_or_create_peer %s %s %p %lu\n", std::string(addr).c_str(), peer ? std::string(peer->addr).c_str() : "null", peer.get(), peer_by_addr.size());
    if (!peer) {
        peer = new_peer(addr);
    }
    return peer;
}


struct stats_t
{
	volatile uint64_t bps = 0;
	volatile uint64_t pps = 0;

    uint64_t last_pps = 0;
    uint64_t last_bps = 0;

    void consume(uint64_t packets, uint64_t bytes)
    {
        __atomic_fetch_add(&pps, packets, 0);
        __atomic_fetch_add(&bps, bytes, 0);
    }

    void print(const char* title, const char* tail)
    {
        uint64_t now_pps = 0, now_bps = 0;
        now_pps += __atomic_load_n(&pps, 0);
        now_bps += __atomic_load_n(&bps, 0);

        double delta_pps = now_pps - last_pps;
        double delta_bps = now_bps - last_bps;
        last_pps = now_pps;
        last_bps = now_bps;

        printf("%s: %7.6fM pps %7.6fMiB / %7.6fMb%s", title,
                delta_pps / 1000.0 / 1000.0,
                delta_bps / 1024.0 / 1024.0,
                delta_bps * 8.0 / 1000.0 / 1000.0,
                tail);
    }
};




#define MTU_SIZE (2048-64*2)
#define MAX_MSG 1024

struct receiver_state {
    net_addr listen_addr;
	int fd;

    stats_t stats;
    stats_t data_stats;

	struct mmsghdr messages[MAX_MSG];
	char buffers[MAX_MSG][MTU_SIZE];
	mdp_iovecs iovecs[MAX_MSG];
    struct sockaddr_in addrs[MAX_MSG];
    mdp_msg_header headers[MAX_MSG];

    receiver_state()
    {
        int i;
    	for (i = 0; i < MAX_MSG; i++) {
            struct mmsghdr *msg = &messages[i];

            msg->msg_hdr.msg_iov = &(iovecs[i].header_iovec);
            msg->msg_hdr.msg_iovlen = 2;
            msg->msg_hdr.msg_name = &addrs[i];
            msg->msg_hdr.msg_namelen = sizeof(addrs[i]);

            iovecs[i].payload_iovec.iov_base = buffers[i];
            iovecs[i].payload_iovec.iov_len = sizeof(buffers[i]);

            iovecs[i].header_iovec.iov_base = &headers[i];
            iovecs[i].header_iovec.iov_len = sizeof(headers[i]);
        }
    }

} __attribute__ ((aligned (64)));

receiver_state rstate;


void peer_add_pending_confirmation(std::shared_ptr<mdp_peer> peer, uint64_t msg_id, uint64_t flags)
{
    //printf("%s: %d %d %d\n", addr_to_str(&peer->addr), peer->id, peer->rcvd.size(), peer->confirmations.size());
    mdp_confirmation c;
    c.flags_and_id = flags | (((uint64_t)MDP_MSG_FLAG_DELIVERED) << 56);

    {
        std::lock_guard<std::mutex> lock(peer->confirmations_mutex);

        peer->confirmations.emplace(msg_id, c);
    }
}

void peer_copy_rcvd_data(std::shared_ptr<mdp_peer> peer, void* ptr, uint32_t size)
{
    std::vector<uint8_t> v(size);
    memcpy(v.data(), ptr, v.size());
    peer->rcvd.push_back(std::move(v));
}

void peer_DEBUG_drop_rcvd_data(std::shared_ptr<mdp_peer> peer)
{
    if (peer->rcvd.size() >= 1000000) {
        std::lock_guard<std::mutex> lock(peer->confirmations_mutex);

        printf("RCVD LIMIT: %lu %lu \n", peer->rcvd.size(), peer->confirmations.size());

        peer->rcvd.clear();

        if (peer->confirmations.size() > 1000000) {
            printf("Confirmations not trimming");
            exit(1);
        }
    }
}

void handle_data_msg(const mdp_msg_header* mdp_msg, const net_addr& peer_addr, const iovec* payload_iovec)
{
    //printf("%s\n", addr_to_str(&peer_addr));

    auto peer = get_or_create_peer(peer_addr);

    uint64_t msg_id = mdp_msg->get_id();

    peer_add_pending_confirmation(peer, msg_id, mdp_msg->flags_and_id);

    peer_copy_rcvd_data(peer, payload_iovec->iov_base, mdp_msg->msg_size_bytes);

    peer_DEBUG_drop_rcvd_data(peer);

    rstate.data_stats.consume(1, mdp_msg->msg_size_bytes);
}

void peer_trim_confirmations(std::shared_ptr<mdp_peer> peer, uint64_t peers_complete_confirmed_msg_id)
{
    std::lock_guard<std::mutex> lock(peer->confirmations_mutex);

    // printf("%d %d %d\n", peer->confirmations.begin()->first, peers_complete_confirmed_msg_id, peer->confirmations.size());

    //printf("peer_trim_confirmations %lu %lu %lu\n", peer->confirmations.size(), peer->confirmations.begin()->first, peers_complete_confirmed_msg_id);

    while (!peer->confirmations.empty() && peer->confirmations.begin()->first <= peers_complete_confirmed_msg_id) {
        peer->confirmations.erase(peer->confirmations.begin()->first);
    }
    // printf("> %d %d %d\n", peer->confirmations.begin()->first, peers_complete_confirmed_msg_id, peer->confirmations.size());
}

void peer_consume_confirmations(std::shared_ptr<mdp_peer> peer, const uint64_t* confirms, int conf_number)
{
    for (int i = 1; i < conf_number; i++) {

        uint64_t confirm_msg_id = confirms[i] & MDP_MSG_ID_MASK;
        //printf("peer_consume_confirmations %d %d %lu\n", conf_number, i, confirm_msg_id);
        // TODO: confirm message delivery

        // update complete_confirmed_msg_id
        // TODO: stub implementation - correct is getting min id of pending data messages (no pending data messages implemented)
        if (peer->complete_confirmed_msg_id < confirm_msg_id) {
            // printf("%d %d\n", peer->complete_confirmed_msg_id, conf_id);
            peer->complete_confirmed_msg_id = confirm_msg_id;
            // printf("> %d %d\n", peer->complete_confirmed_msg_id, conf_id);
        }
    }
}

void handle_confirm_msg(const mdp_msg_header* mdp_msg, const net_addr& peer_addr, const iovec* payload_iovec)
{
    uint64_t* confirms = (uint64_t*)payload_iovec->iov_base;
    int conf_number = mdp_msg->msg_size_bytes / sizeof(uint64_t) - 1;
    // printf("%d %d\n", mdp_msg->msg_size_bytes, conf_number);

    auto peer = get_or_create_peer(peer_addr);

    //printf("handle_confirm_msg %d\n", conf_number);

    if (conf_number > 0) {
        // Producer side (data message sender)
        peer_consume_confirmations(peer, confirms + 1, conf_number);
    }
    // Consumer side (data message receiver)
    peer_trim_confirmations(peer, *confirms);
}

void handle_service_msg(const mdp_msg_header* mdp_msg, const net_addr& peer_addr, const iovec* payload_iovec)
{
    if ((mdp_msg->get_flags() & MDP_MSG_FLAG_SERVICE_CONFIRMATION) == MDP_MSG_FLAG_SERVICE_CONFIRMATION) {
        handle_confirm_msg(mdp_msg, peer_addr, payload_iovec);
    }
}

void handle_rcvd_msg(const mmsghdr* msg, size_t length)
{
    //printf(">> %p %d %p %d\n", msg->msg_hdr.msg_name, msg->msg_hdr.msg_namelen, msg->msg_hdr.msg_control, msg->msg_hdr.msg_controllen);

    net_addr peer_addr(&msg->msg_hdr);

    const mdp_msg_header* mdp_msg = (const mdp_msg_header*) msg->msg_hdr.msg_iov->iov_base;
    //printf("%s: %d 0x%02X %llu %d\n", addr_to_str(&peer_addr), msg->msg_len, mdp_msg->get_flags(), mdp_msg->get_id(), mdp_msg->msg_size_bytes);

    iovec* payload_iovec = &msg->msg_hdr.msg_iov[1];

    //printf("handle_rcvd_msg 0x%02X 0x%016lX\n", mdp_msg->get_flags(), mdp_msg->flags_and_id);

    if ((mdp_msg->get_flags() & MDP_MSG_FLAG_SERVICE) == 0) {
        handle_data_msg(mdp_msg, peer_addr, payload_iovec);
    }

    if ((mdp_msg->get_flags() & MDP_MSG_FLAG_SERVICE) == MDP_MSG_FLAG_SERVICE) {
        handle_service_msg(mdp_msg, peer_addr, payload_iovec);
    }
}

static void handle_rcvd_msgs(int rcvd_count)
{
    int i, bytes = 0;
    for (i = 0; i < rcvd_count; i++) {
        struct mmsghdr *msg = &rstate.messages[i];
        /* char *buf = msg->msg_hdr.msg_iov->iov_base; */
        int len = msg->msg_len;

        handle_rcvd_msg(msg, msg->msg_len);

        msg->msg_hdr.msg_flags = 0;
        msg->msg_len = 0;
        bytes += len;
    }
    rstate.stats.consume(rcvd_count, bytes);
}

static void receiver_thread_task()
{
    /* Blocking recv. */
    int r = recvmmsg(rstate.fd, &rstate.messages[0], MAX_MSG, MSG_WAITFORONE, NULL);
    //printf("recvmmsg %d\n", r);
    if (r <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return;
        }
        fprintf(stderr, "FATAL: recvmmsg()");
        exit(EXIT_FAILURE);
    }

    handle_rcvd_msgs(r);
}

static void receiver_thread_loop()
{
	while (1) {
        receiver_thread_task();
	}
}

char _payload[1024] = "0123456789_0123456789_0123456789";

struct sender_state
{
    bool gen_load = false;

    int64_t last_id = 1;

	int socket_fd;

	net_addr target_addr;

	int messages_capacity;
    int msg_count;
    struct mmsghdr *messages;

	int payload_max_size;
    char* payloads = nullptr;

    mdp_iovecs *iovecs;
    mdp_msg_header *headers;

	stats_t stats;
    stats_t data_stats; // count confirmed data

    sender_state()
    {
        payload_max_size = 1024;
		messages_capacity = 32;
        msg_count = 0;

        messages = (mmsghdr*)calloc(messages_capacity, sizeof(struct mmsghdr));
        iovecs = (mdp_iovecs*)calloc(messages_capacity, sizeof(struct mdp_iovecs));
        headers = (mdp_msg_header*)calloc(messages_capacity, sizeof(struct mdp_msg_header));
        payloads = (char*) malloc(messages_capacity * payload_max_size);

        for (int i = 0; i < messages_capacity; i++) {
            struct mdp_iovecs *iovec = &iovecs[i];
            struct mmsghdr *msg = &messages[i];

            msg->msg_hdr.msg_iov = &iovec->header_iovec;
            msg->msg_hdr.msg_iovlen = 2;

            iovec->payload_iovec.iov_base = (void*) (payloads + i*payload_max_size);
            iovec->payload_iovec.iov_len = 0;

            iovec->header_iovec.iov_base = (void*)&headers[i];
            iovec->header_iovec.iov_len = sizeof(headers[i]);
            headers[i].crc32 = 0;
            headers[i].multipart_msg_block_seq_num = 0;
            headers[i].msg_size_bytes = 0;
            headers[i].flags_and_id = 0;
        }
    }
};

sender_state sstate;

void* sstate_prepare_msg(const net_addr& addr, uint32_t size, uint64_t flags_and_id)
{
    if (sstate.msg_count >= sstate.messages_capacity) {
        return nullptr;
    }

    if (size > sstate.payload_max_size) {
        // Not implemented
        return nullptr;
    }

    // get message pointer
    struct mmsghdr *msg = &(sstate.messages[sstate.msg_count]);

    // fill UDP destination address
    msg->msg_hdr.msg_name = addr.get_sockaddr();
    msg->msg_hdr.msg_namelen = addr.get_sockaddr_len();

    // fill MDP message header
    mdp_msg_header* h = &sstate.headers[sstate.msg_count];
    h->flags_and_id = flags_and_id;
    h->msg_size_bytes = size;
    h->multipart_msg_block_seq_num = 0;
    h->crc32 = 0xCCCCCCCC; // not implemented

    // header iovec is already filled in sender_state constructor
    mdp_iovecs* iov = &sstate.iovecs[sstate.msg_count];
    iov->payload_iovec.iov_len = size;

    sstate.msg_count++;

    return iov->payload_iovec.iov_base;
}

void* sstate_prepare_data_msg(const net_addr& addr, uint32_t size, int64_t msg_id)
{
    // TODO: msg_id >>>>> peer->last_sended_msg_id
    return sstate_prepare_msg(addr, size, msg_id);
}

void* sstate_prepare_service_msg(const net_addr& addr, uint8_t flags, uint32_t size)
{
    return sstate_prepare_msg(addr, size, MDP_MSG_FLAGS(MDP_MSG_FLAG_SERVICE | flags));
}

void sstate_prepare_peer_confirm_msgs(std::shared_ptr<mdp_peer> peer)
{
    auto conf_it = peer->confirmations.begin();
    auto end = peer->confirmations.end();
    size_t pending_confirms_count = peer->confirmations.size();

    while (conf_it != end && sstate.msg_count < sstate.messages_capacity) {

        int confirms_capacity = sstate.payload_max_size / sizeof(uint64_t) - 1;

        if (pending_confirms_count < confirms_capacity) {
            confirms_capacity = pending_confirms_count;
        }

        uint32_t size = (confirms_capacity + 1) * sizeof(uint64_t);

        uint64_t* confirms = (uint64_t*) sstate_prepare_service_msg(peer->addr, MDP_MSG_FLAG_SERVICE_CONFIRMATION, size);
        if (confirms == nullptr) {
            break;
        }

        *confirms++ = peer->complete_confirmed_msg_id;

        // fill MDP table of confirmations
        for (int confirms_count = 0; conf_it != end && confirms_count < confirms_capacity; conf_it++, confirms_count++) {
            *confirms++ = conf_it->second.flags_and_id;
        }

        pending_confirms_count -= confirms_capacity;
    }
}

void sstate_prepare_peer_heartbeat_msg(std::shared_ptr<mdp_peer> peer)
{
    if (peer->last_heartbeat_sent + 1s > std::chrono::steady_clock::now()) {
        return;
    }

    uint64_t* confirms = (uint64_t*) sstate_prepare_service_msg(peer->addr, MDP_MSG_FLAG_SERVICE_CONFIRMATION, sizeof(uint64_t));
    if (confirms == nullptr) {
        return;
    }

    *confirms = peer->complete_confirmed_msg_id;

    peer->last_heartbeat_sent = std::chrono::steady_clock::now() + 1s;
}

void sstate_prepare_peer_confirms(std::shared_ptr<mdp_peer> peer)
{
    std::lock_guard<std::mutex> lock(peer->confirmations_mutex);

    if (peer->confirmations.empty()) {
        sstate_prepare_peer_heartbeat_msg(peer);
    } else {
        sstate_prepare_peer_confirm_msgs(peer);
    }
}

static void sender_prepare_confirms()
{
    auto all_peers = get_all_peers();

    for (auto peer : all_peers) {
        sstate_prepare_peer_confirms(peer);
    }
    //printf("sender_prepare_confirms %d\n", sstate.msg_count);
}

int64_t number_of_load_messages = 1;

static void sender_generate_load()
{
    for (;
        sstate.msg_count < sstate.messages_capacity && number_of_load_messages > 0;
        sstate.msg_count++, number_of_load_messages--)
    {
        //TODO: Nobody waits for confirmation, because peer not created yet, because data msgs generated inside sender thread
        void* data = sstate_prepare_data_msg(sstate.target_addr, sstate.payload_max_size, sstate.last_id++); // peer->last_sended_msg_id
        if (data) {
            memset(data, 0xAB, sstate.payload_max_size);
        }
    }
}

static void sender_send_messages()
{
    if (sstate.msg_count == 0) {
        std::this_thread::sleep_for(1ms);
        return;
    }

    // Send packets
    //printf("sendmmsg %d\n", sstate.msg_count);
    int r = sendmmsg(sstate.socket_fd, sstate.messages, sstate.msg_count, 0);
    //TODO: handle not sent messages
    if (r <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return;
        }

        if (errno == ECONNREFUSED) {
            return;
        }
        fprintf(stderr, "sendmmsg() %d\n", errno);
        exit(EXIT_FAILURE);
    }
    if (r < sstate.msg_count) {
        printf("sendmmsg incomplete %d < %d\n", r, sstate.msg_count);
    }

    // Count sent bytes and messages
    int i, bytes = 0;
    for (i = 0; i < r; i++) {
        struct mmsghdr *msg = &sstate.messages[i];
        /* char *buf = msg->msg_hdr.msg_iov->iov_base; */
        bytes += msg->msg_len;
        msg->msg_hdr.msg_flags = 0;
        msg->msg_len = 0;
    }
    sstate.stats.consume(r, bytes);
}

static void sender_thread_task()
{
    sstate.msg_count = 0;

    // First - put confirmations into packets buffer
    sender_prepare_confirms();

    // Next - put generated payload into packets buffer
    if (sstate.gen_load) {
        sender_generate_load();
    }

    sender_send_messages();

    //std::this_thread::sleep_for(1s);
}

static void sender_thread_loop()
{
	while (1) {
        sender_thread_task();
	}
}


std::thread receiver_thread;
std::thread sender_thread;

void mdp_start_receiver(const char* listen_addr, uint16_t listen_port)
{
    int recv_buf_size = 4*1024;

    rstate.listen_addr.from_str(listen_addr, listen_port);

    // Bind receiver

    fprintf(stderr, "[*] Starting udpreceiver on %s, recv buffer %iKiB\n",
                    addr_to_str(&rstate.listen_addr), recv_buf_size / 1024);

    rstate.fd = net_bind_udp(&rstate.listen_addr);
    net_set_buffer_size(rstate.fd, recv_buf_size, 0);

    // Start receiver thread

    fprintf(stderr, "[*] Starting receiver thread\n");

    receiver_thread = std::thread(receiver_thread_loop);
}

void mdp_start_sender(const char* dest_addr, uint16_t dest_port)
{
    // Bind and connect sender

    sstate.socket_fd = rstate.fd; // No need to bind or connect - just use same socket for sending as for receiving

    if (dest_addr != nullptr) {

        sstate.gen_load = true;

        sstate.target_addr.from_str(dest_addr, dest_port);
    }

    // Start sender thread

    fprintf(stderr, "[*] Starting sender thread\n");
    sender_thread = std::thread(sender_thread_loop);
}




uint64_t mdp_connect(const char* host, uint16_t port)
{

    return 0;
}

uint64_t mdp_enqueue(uint64_t peer_id, void* data, size_t length, size_t timeout_us)
{

    return 0;
}

std::list<std::vector<uint8_t>> mdp_get_rcvd(uint64_t peer_id)
{


    return {};
}

bool mdp_is_delivered(uint64_t peer_id, uint64_t msg_id)
{
    auto peer = get_peer_by_id(peer_id);
    if (!peer) {
        return false;
    }

    if (peer->complete_confirmed_msg_id >= msg_id) {
        return true;
    }
    if (msg_id > peer->last_sended_msg_id) {
        return false;
    }
    auto msg_it = peer->transmissions.find(msg_id);
    if (msg_it == peer->transmissions.end()) {
        return true;
    }
    return false;
}



void mdp_print_state()
{
    sstate.stats.print("send", ";  ");
    rstate.stats.print("rcvd", ";  ");
    rstate.data_stats.print("data", "\n");
}
