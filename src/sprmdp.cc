
#include "sprmdp.hh"

#include <map>
#include <memory>
#include <chrono>
#include <thread>
#include <string>
#include <iostream>
#include <cstring>
#include <mutex>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "net.h"


struct sprmdp_transmission
{
    uint64_t id;

    sprmdp_msg_header header;

    std::vector<uint8_t> data;

    int retransmit_count = 0;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point next_restransmit;
};

struct sprmdp_confirmation
{
    uint64_t flags_and_id;

    int retransmit_count = 0;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point next_restransmit;
};

struct sprmdp_peer
{ //TODO: receiver and sender
    uint64_t id;
    net_addr addr;

    uint64_t last_sended_msg_id = 0;
    uint64_t complete_confirmed_msg_id = 0;

    int client_socket_fd = -1;

    // map of started transmissions with confirmation waiters
    std::map<uint64_t, sprmdp_transmission> transmissions;

    // map of receiving multipart messages
    // not implemented

    // map of confirmations, waiting for trimming by peer's complete_confirmed_msg_id
    std::map<uint64_t, sprmdp_confirmation> confirmations;
    std::mutex confirmations_mutex;

    // list of received messages, sinle-packet messages goes directly here
    std::list<std::vector<uint8_t>> rcvd;
};


struct sprmdp_iovecs
{
    iovec header_iovec;
    iovec payload_iovec;
} __attribute__((packed));

uint64_t last_peer_id = 0;

std::map<uint64_t, std::shared_ptr<sprmdp_peer>> peer_by_id;
std::map<net_addr, std::shared_ptr<sprmdp_peer>> peer_by_addr;

#define MTU_SIZE (2048-64*2)
#define MAX_MSG 1024

struct receiver_state {
	int fd;
	volatile uint64_t bps;
	volatile uint64_t pps;
	struct mmsghdr messages[MAX_MSG];
	char buffers[MAX_MSG][MTU_SIZE];
	sprmdp_iovecs iovecs[MAX_MSG];
    struct sockaddr_in addrs[MAX_MSG];
    sprmdp_msg_header headers[MAX_MSG];

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

void handle_rcvd_msg(const mmsghdr* msg, size_t length)
{
    //printf(">> %p %d %p %d\n", msg->msg_hdr.msg_name, msg->msg_hdr.msg_namelen, msg->msg_hdr.msg_control, msg->msg_hdr.msg_controllen);


    struct net_addr peer_addr;
    peer_addr.ipver = 4;
    peer_addr.sockaddr_len = sizeof(peer_addr.sin4);
    peer_addr.sockaddr = (sockaddr*)&peer_addr.sin4;
    memcpy(peer_addr.sockaddr, msg->msg_hdr.msg_name, peer_addr.sockaddr_len);

    const sprmdp_msg_header* mdp_msg = (const sprmdp_msg_header*) msg->msg_hdr.msg_iov->iov_base;
    //printf("%s: %d 0x%02X %llu %d\n", addr_to_str(&peer_addr), msg->msg_len, mdp_msg->get_flags(), mdp_msg->get_id(), mdp_msg->msg_size_bytes);

    iovec* payload_iovec = &msg->msg_hdr.msg_iov[1];

    if ((mdp_msg->get_flags() & SPRMDP_MSG_FLAG_SERVICE) == 0) {

        //printf("xxx\n");

        auto peer_it = peer_by_addr.find(peer_addr);
        if (peer_it == peer_by_addr.end()) {

            //printf("YYY\n");

            last_peer_id++;
            auto peer = std::make_shared<sprmdp_peer>();
            peer->id = last_peer_id;
            peer->addr = peer_addr;
            //printf("YYY\n");

            auto it = peer_by_addr.emplace(std::make_pair(peer->addr, peer));
            peer_it = it.first;
            //printf("YYYY\n");

            peer_by_id.emplace(std::make_pair(peer->id, peer));
        }

        //printf("xxx\n");

        auto peer = peer_it->second;
        uint64_t msg_id = mdp_msg->get_id();

        //printf("%s: %d %d %d\n", addr_to_str(&peer->addr), peer->id, peer->rcvd.size(), peer->confirmations.size());

        sprmdp_confirmation c;
        c.flags_and_id = mdp_msg->flags_and_id | (((uint64_t)SPRMDP_MSG_FLAG_DELIVERED) << 56);

        {
            std::lock_guard<std::mutex> lock(peer->confirmations_mutex);
            peer->confirmations[msg_id] = c;
        }

        std::vector<uint8_t> v(mdp_msg->msg_size_bytes);
        memcpy(v.data(), payload_iovec->iov_base, v.size());
        peer->rcvd.push_back(std::move(v));

        if (peer->rcvd.size() >= 1000000) {
            std::lock_guard<std::mutex> lock(peer->confirmations_mutex);
            printf("RCVD LIMIT: %d %d \n", peer->rcvd.size(), peer->confirmations.size());
            peer->rcvd.clear();
            if (peer->confirmations.size() > 1000000) {
                peer->confirmations.clear();
            }
        }
    }

    if ((mdp_msg->get_flags() & SPRMDP_MSG_FLAG_SERVICE) == SPRMDP_MSG_FLAG_SERVICE) {
        uint64_t* confirms = (uint64_t*)payload_iovec->iov_base;
        int conf_number = mdp_msg->msg_size_bytes / 8;
        // printf("%d %d\n", mdp_msg->msg_size_bytes, conf_number);

        auto peer_it = peer_by_addr.find(peer_addr);
        if (peer_it != peer_by_addr.end()) {
            auto peer = peer_it->second;

            if (conf_number > 0) {
                uint64_t peers_complete_confirmed_msg_id = confirms[0];
                std::lock_guard<std::mutex> lock(peer->confirmations_mutex);

                // printf("%d %d %d\n", peer->confirmations.begin()->first, peers_complete_confirmed_msg_id, peer->confirmations.size());

                while (!peer->confirmations.empty() && peer->confirmations.begin()->first <= peers_complete_confirmed_msg_id) {
                    peer->confirmations.erase(peer->confirmations.begin()->first);
                }
                // printf("> %d %d %d\n", peer->confirmations.begin()->first, peers_complete_confirmed_msg_id, peer->confirmations.size());
            }

            for (int i = 1; i < conf_number; i++) {
                uint64_t conf_id = confirms[i] & SPRMDP_MSG_ID_MASK;
                // TODO: confirm msg delivery

                // update complete_confirmed_msg_id
                // TODO: stub implementation - correct is getting min id of pending messages
                if (peer->complete_confirmed_msg_id < conf_id) {
                    // printf("%d %d\n", peer->complete_confirmed_msg_id, conf_id);
                    peer->complete_confirmed_msg_id = conf_id;
                    // printf("> %d %d\n", peer->complete_confirmed_msg_id, conf_id);
                }
            }
        }
    }

}

static void receiver_thread_loop(void *userdata)
{
	receiver_state *state = (receiver_state*)userdata;

	while (1) {
		/* Blocking recv. */
		int r = recvmmsg(state->fd, &state->messages[0], MAX_MSG, MSG_WAITFORONE, NULL);
		if (r <= 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
				continue;
			}
			fprintf(stderr, "FATAL: recvmmsg()");
            exit(EXIT_FAILURE);
		}

		int i, bytes = 0;
		for (i = 0; i < r; i++) {
			struct mmsghdr *msg = &state->messages[i];
			/* char *buf = msg->msg_hdr.msg_iov->iov_base; */
			int len = msg->msg_len;

            handle_rcvd_msg(msg, msg->msg_len);

			msg->msg_hdr.msg_flags = 0;
			msg->msg_len = 0;
			bytes += len;
		}
		__atomic_fetch_add(&state->pps, r, 0);
		__atomic_fetch_add(&state->bps, bytes, 0);
	}
}

char _payload[1024] = "0123456789_0123456789_0123456789";

struct sender_state
{
    int64_t last_id = 1;
	int fd;
	struct net_addr target_addr;
	int packets_in_buf;
	const char *payload;
	int payload_sz;
	int src_port;

    struct mmsghdr *messages;
    sprmdp_iovecs *iovecs;
    sprmdp_msg_header *headers;

    sender_state()
    {
		packets_in_buf = 1024;
		payload = _payload;
		payload_sz = sizeof(_payload);
        src_port = 0;

        messages = (mmsghdr*)calloc(packets_in_buf, sizeof(struct mmsghdr));
        iovecs = (sprmdp_iovecs*)calloc(packets_in_buf, sizeof(struct sprmdp_iovecs));
        headers = (sprmdp_msg_header*)calloc(packets_in_buf, sizeof(struct sprmdp_msg_header));

        for (int i = 0; i < packets_in_buf; i++) {
            struct sprmdp_iovecs *iovec = &iovecs[i];
            struct mmsghdr *msg = &messages[i];

            msg->msg_hdr.msg_iov = &iovec->header_iovec;
            msg->msg_hdr.msg_iovlen = 2;

            iovec->payload_iovec.iov_base = (void*)payload;
            iovec->payload_iovec.iov_len = payload_sz;

            iovec->header_iovec.iov_base = (void*)&headers[i];
            iovec->header_iovec.iov_len = sizeof(headers[i]);
            headers[i].crc32 = 0;
            headers[i].multipart_msg_block_seq_num = 0;
            headers[i].msg_size_bytes = payload_sz;
            headers[i].flags_and_id = 0;
        }
    }
};

sender_state sstate;

static void sender_thread_loop(void *userdata)
{
	sender_state *state = (sender_state*)userdata;

	while (1) {
        int msg_counter = 0;
        for (auto peer_it : peer_by_addr) {
            auto peer = peer_it.second;

            int conf_packed = 0;
            auto conf_it = peer->confirmations.begin();
            while (conf_packed < peer->confirmations.size()) {

                struct mmsghdr *msg = &(state->messages[msg_counter]);
                msg->msg_hdr.msg_name = state->target_addr.sockaddr;
                msg->msg_hdr.msg_namelen = state->target_addr.sockaddr_len;

                state->headers[msg_counter].flags_and_id = 0;
                state->headers[msg_counter].set_flags(SPRMDP_MSG_FLAG_SERVICE | SPRMDP_MSG_FLAG_SERVICE_CONFIRMATION);

                state->headers[msg_counter].msg_size_bytes = 8;
                uint64_t* confirms = (uint64_t*) state->iovecs[msg_counter].payload_iovec.iov_base;
                confirms[0] = peer->complete_confirmed_msg_id;
                int conf_countr = 1;

                {
                    std::lock_guard<std::mutex> lock(peer->confirmations_mutex);
                    for (; conf_it != peer->confirmations.end(); conf_it++) {
                        confirms[conf_countr] = conf_it->second.flags_and_id;

                        conf_packed++;
                        conf_countr++;
                        if (conf_countr >= state->payload_sz / 8) {
                            break;
                        }
                    }
                }
                state->headers[0].msg_size_bytes = conf_countr * 8;

                msg_counter++;
                if (msg_counter >= state->packets_in_buf) {
                    break;
                }
            }
        }

        for (; msg_counter < state->packets_in_buf; msg_counter++) {
            struct mmsghdr *msg = &(state->messages[msg_counter]);
            msg->msg_hdr.msg_name = state->target_addr.sockaddr;
            msg->msg_hdr.msg_namelen = state->target_addr.sockaddr_len;

            state->headers[msg_counter].flags_and_id = state->last_id++; // peer->last_sended_msg_id
        }

		int r = sendmmsg(state->fd, state->messages, state->packets_in_buf, 0);
		if (r <= 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
				continue;
			}

			if (errno == ECONNREFUSED) {
				continue;
			}
			fprintf(stderr, "sendmmsg()\n");
            exit(EXIT_FAILURE);
		}
		int i, bytes = 0;
		for (i = 0; i < r; i++) {
			struct mmsghdr *msg = &state->messages[i];
			/* char *buf = msg->msg_hdr.msg_iov->iov_base; */
			int len = msg->msg_len;
			msg->msg_hdr.msg_flags = 0;
			msg->msg_len = 0;
			bytes += len;
		}
	}
}


std::thread receiver_thread;
std::thread sender_thread;

void start_receiver(uint16_t port)
{
}

void start_sender(uint16_t port)
{
}

int sprmdp_init(uint16_t port, uint32_t block_size, size_t confirmation_max_wait_us)
{
	int recv_buf_size = 4*1024;

    struct net_addr listen_addr;
	parse_addr(&listen_addr, "0.0.0.0", port);

    fprintf(stderr, "[*] Starting udpreceiver on %s, recv buffer %iKiB\n",
                    addr_to_str(&listen_addr), recv_buf_size / 1024);
    rstate.fd = net_bind_udp(&listen_addr);
    net_set_buffer_size(rstate.fd, recv_buf_size, 0);

    // start receiver threads
    fprintf(stderr, "[*] Starting receiver thread\n");
    receiver_thread = std::thread(receiver_thread_loop, &rstate);

    sstate.fd = rstate.fd;

	parse_addr(&sstate.target_addr, "127.0.0.1", port);
    //sstate.src_port = 11084;

    //fprintf(stderr, "[*] Sending to %s, from port %d, send buffer %i packets\n",
    //    addr_to_str(&sstate.target_addr), sstate.src_port, sstate.packets_in_buf);
    //sstate.fd = net_connect_udp(&sstate.target_addr, sstate.src_port);

    fprintf(stderr, "[*] Starting sender thread\n");
    sender_thread = std::thread(sender_thread_loop, &sstate);

    return 0;
}

uint64_t sprmdp_connect(const char* host, uint16_t port)
{

    return 0;
}

uint64_t sprmdp_enqueue(uint64_t peer_id, void* data, size_t length, size_t timeout_us)
{

    return 0;
}

std::list<std::vector<uint8_t>> sprmdp_get_rcvd(uint64_t peer_id)
{


    return {};
}

bool sprmdp_is_delivered(uint64_t peer_id, uint64_t msg_id)
{
    auto peer_it = peer_by_id.find(peer_id);
    if (peer_it == peer_by_id.end()) {
        return false;
    }
    auto& peer = peer_it->second;
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


uint64_t last_pps = 0;
uint64_t last_bps = 0;

void sprmdp_print_state()
{
    uint64_t now_pps = 0, now_bps = 0;
    now_pps += __atomic_load_n(&rstate.pps, 0);
    now_bps += __atomic_load_n(&rstate.bps, 0);

    double delta_pps = now_pps - last_pps;
    double delta_bps = now_bps - last_bps;
    last_pps = now_pps;
    last_bps = now_bps;

    printf("%7.3fM pps %7.3fMiB / %7.3fMb\n",
            delta_pps / 1000.0 / 1000.0,
            delta_bps / 1024.0 / 1024.0,
            delta_bps * 8.0 / 1000.0 / 1000.0 );
}
