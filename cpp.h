#pragma once

#include "textfile.h"

namespace cpp {

struct package_dependency {
    using names = std::list<std::string>;
    names modules;
    names includes;
};

inline auto scan_package_dependency(std::filesystem::path p, std::size_t depth) {
    package_dependency pd;
    std::ifstream is(p);

    for(auto const&[i, l]: std::views::zip(
                                std::views::iota(std::size_t(0), depth),
                                std::ranges::subrange(textfile::in_it(is), textfile::in_it())
                                )) {
        static std::regex regex_includes("#include\\s+<(.*)>");
        static std::regex regex_modules("import\\s+([^;]*);.*");
        std::smatch m;
        std::regex_match(l, m, regex_modules);
        if(m.size() >= 2)
            pd.modules.push_back(m[1].str());
        std::regex_match(l, m, regex_includes);
        if(m.size() == 2)
            pd.includes.push_back(m[1].str());
     }
    return pd;
}

} // cpp
