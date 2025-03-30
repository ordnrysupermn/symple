#pragma once
import std;

#include "config.h"

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

using module_names = std::list<std::string>;
module_names scan_modules(std::filesystem::path p, std::size_t depth);

} // textfile



