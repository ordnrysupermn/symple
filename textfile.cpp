#include <cstdlib>

import std;

#include "textfile.h"

namespace textfile {

void cat(std::filesystem::path p, std::size_t max) {
    std::ifstream is(p);
    for(auto const&[i, l]: std::views::zip(
                                std::views::iota(std::size_t(0), max),
                                std::ranges::subrange(in_it(is), in_it())
                                )) {
        std::println("{:}", l.string());
    }
}

int touch(std::filesystem::path p) {
    {
        std::ofstream os(p);
    }
    return std::filesystem::exists(p) ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // textfile



