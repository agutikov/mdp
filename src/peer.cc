
#include "mdp.hh"
#include <chrono>
#include <thread>
using namespace std::chrono_literals;

int main(int argc, const char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage:\n\t %s <listen_addr:listen_port> [<dest_addr:dest_port>]\n", argv[0]);
        exit(1);
    }


    mdp_start_receiver(argv[1], 0);

    if (argc == 3) {
        mdp_start_sender(argv[2], 0);
    } else {
        mdp_start_sender(nullptr, 0);
    }



    while (true) {
        mdp_print_state();
        std::this_thread::sleep_for(1s);
    }

    return 0;
}

