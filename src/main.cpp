#include "uci.hpp"
#include "attacks.hpp"
#include "zobrist.hpp"

int main() {
    chess::attacks::init();
    chess::zobrist::init();
    chess::uci_loop();
    return 0;
}
