#pragma once
import std;

#include "config.h"
#include "defs.h"

namespace textfile {

// TODO: strong typedef
struct line : std::string {
    auto operator<=>(line const&) const = default;
    auto string() const {return static_cast<std::string>(*this);}
    friend std::istream & operator>>(std::istream &is, line& l) {   
        return std::getline(is, l);
    }
    friend std::ostream & operator<<(std::ostream &os, line& l) {
        return os << l.string();
    }
};
using in_it = std::istream_iterator<line>;

void cat(std::filesystem::path p, std::size_t max = defaults::max_cat_lines);

struct package_dependency {
    using names = std::list<std::string>;
    names modules;
    names includes;
};

inline auto scan_lines_regex(auto name, auto regex) {
    std::list<std::smatch> matches;
    std::regex r{regex};
    std::ifstream is(name);
    for(auto const&l: std::ranges::subrange(textfile::in_it(is), textfile::in_it())) {
        std::smatch m;
        std::regex_match(l, m, r);
        if(!m.empty())
            matches.push_back(m);
    }
    return matches;
}

inline auto scan_lines(auto name, auto const&regexes, std::function<void(std::smatch const&)> fx) {
    std::ifstream is(name);
    for(auto const&l: std::ranges::subrange(textfile::in_it(is), textfile::in_it())) {
        for(auto &r: regexes) {
            std::smatch m;
            std::regex_match(l, m, r);
            if(m.empty())
                continue;
            fx(m);
        }
    }
}

inline auto scan_package_dependency(std::filesystem::path p, std::size_t depth) {
    package_dependency pd;
    std::ifstream is(p);

    for(auto const&[i, l]: std::views::zip(
                                std::views::iota(std::size_t(0), depth),
                                std::ranges::subrange(in_it(is), in_it())
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

} // textfile



