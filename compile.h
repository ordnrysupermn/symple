#pragma once

import std;

#include "defs.h"

namespace compile {

struct fragment {
    fragment() = default;
    fragment(std::string s) : value(s) {};
    std::string value;
};

struct command {
    auto & operator<<(auto frag) {this->value += " " + frag.get();return *this;}

    auto get() const {return this->value;}
    auto spawn(auto const&build_log) {return std::system(std::string(get() + ">" + build_log.string() + " 2>&1").c_str());}
    std::string value;
};

struct compiler : fragment {
    using fragment::fragment;
    auto operator<<(auto frag){return command{this->value} << frag;}
    auto get() const {return this->value;}
};

struct std_standard : fragment {
    using fragment::fragment;
    auto get() const {return std::string("-std=") + this->value;}
};
struct warnings : fragment {
    using fragment::fragment;
    auto get() const {return std::string("-W") + this->value;}
};
struct verbose : fragment {
    verbose(bool verbose) : fragment(verbose ? std::string("--verbose") : std::string()) {}
    auto get() const {return this->value;}
};
struct compile_only : fragment {
    compile_only() = default;
    auto get() const {return std::string("-c");}
};
struct custom : fragment {
    using fragment::fragment;
    auto get() const {return this->value;}
};
struct std_lib : fragment {
    using fragment::fragment;
    auto get() const {return std::string("-stdlib=") + this->value;}
};
struct archive : fragment {
    using fragment::fragment;
    auto get() const {return std::string("-o ") + this->value;}
};
struct includes : fragment {
    includes(auto const&includes) {
        for(auto const&i: includes)
            value += std::format("-I {:} ", i.string());
    }
    auto get() const {return this->value;}
};
struct libraries : fragment {
    libraries(auto const&libraries) {
        for(auto const&l: libraries)
            value += std::format("-L {:} ", l.string());
    }
    auto get() const {return this->value;}
};
struct library : fragment {
    using fragment::fragment;
    auto get() const {return std::string("-l") + this->value;}
};

inline auto append_extension(std::filesystem::path p, std::string ext) {
    p += ext;
    return p;
}

struct compilation_unit {
    auto input() const {return this->path;}

    std::filesystem::path path;
};

} // compile

