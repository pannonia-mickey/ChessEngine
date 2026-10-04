#include <iostream>

#include "magic.hpp"
#include "uci.hpp"

int main() {
    std::ios::sync_with_stdio(false);
    // Search the magic numbers now rather than on the first move generation.
    static_cast<void>(chess::slider_tables());
    chess::Uci uci(std::cin, std::cout);
    uci.loop();
    return 0;
}
