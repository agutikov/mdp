
#include "mdp.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>

using namespace std::chrono_literals;


int main(int argc, const char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage:\n\t %s listen_addr listen_port  [peer_addr peer_port]\n", argv[0]);
        exit(1);
    }

    udp::resolver resolver(io_context);

    udp::endpoint bind_ep = *resolver.resolve(argv[1], argv[2]).begin();

    printf("sizeof(endpoint) = %d\n", sizeof(bind_ep));

    mdp_start_receiver(bind_ep);

    if (argc == 5) {
        udp::endpoint peer_ep = *resolver.resolve(argv[3], argv[4]).begin();

        mdp_start_sender(peer_ep);
    } else {
        mdp_start_sender();
    }


    while (true) {
        mdp_print_state();
        std::this_thread::sleep_for(1s);
    }

    return 0;
}

