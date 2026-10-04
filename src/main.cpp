#include <iostream>

#include "uci.hpp"

int main() {
    std::ios::sync_with_stdio(false);
    chess::Uci uci(std::cin, std::cout);
    uci.loop();
    return 0;
}
