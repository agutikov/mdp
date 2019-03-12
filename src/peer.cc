
#include "sprmdp.hh"
#include <chrono>
#include <thread>
using namespace std::chrono_literals;

int main(int argc, const char* argv[])
{

    sprmdp_init(9874, 1024);



    while (true) {
        sprmdp_print_state();
        std::this_thread::sleep_for(1s);
    }

    return 0;
}

