import std;

#include "tools.h"

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

module_names scan_modules(std::filesystem::path p, std::size_t depth) {
    module_names ms;
    std::ifstream is(p);

    for(auto const&[i, l]: std::views::zip(
                                std::views::iota(std::size_t(0), depth),
                                std::ranges::subrange(in_it(is), in_it())
                                )) {
        static std::regex regex("import\\s+(.*);");
        std::smatch m;
        std::regex_match(l, m, regex);
        if(m.size() == 2)
            ms.push_back(m[1].str());
    }
    return ms;
}

} // textfile



