#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <ranges>
#include <random>
#include <string>

int main(int argc, char *argv[]) {
    if(argc < 4) {
        std::cerr << "Usage: " << argv[0] << " FILENAME BATCH-NUM BATCH-SIZE\n";
        return -1;
    }

    auto filename = argv[1];
    std::uint32_t batches  = std::stoul(argv[2]);
    std::uint32_t batchsize = std::stoul(argv[3]);

    auto out = std::ofstream { filename };

    auto alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        ;
    auto rng = std::mt19937 { 12345 };
    auto dist = std::uniform_int_distribution<int> { 0, 62 };

    auto buf = std::vector<char>(batchsize);

    out.write(reinterpret_cast<char const *>(&batches), sizeof batches);
    out.write(reinterpret_cast<char const*>(&batchsize), sizeof batchsize);

    for([[maybe_unused]] auto i : std::views::iota(0u, batches)) {
        for(auto &c : buf) {
            c = alphabet[dist(rng)];
        }

        out.write(buf.data(), batchsize);
    }
}
