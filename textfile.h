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

} // textfile



