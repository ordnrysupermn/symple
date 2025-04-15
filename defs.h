#pragma once

#include "config.h"

#define printv(...) do{if(options::verbose)std::print(__VA_ARGS__);}while(false)
#define printlnv(...) do{if(options::verbose)std::println(__VA_ARGS__);}while(false)
#define printv2(...) do{if(options::dependency_verbose)std::print(__VA_ARGS__);}while(false)
#define printlnv2(...) do{if(options::dependency_verbose)std::println(__VA_ARGS__);}while(false)

namespace options {
    extern bool verbose;
    extern bool dependency_verbose;
    extern bool compile_verbose;
    extern std::filesystem::path build_log;
};

struct indenter {
    static constexpr int tabsize = 4;
    indenter(int&i) : i(i) {i+=tabsize;}
    ~indenter() {i-=tabsize;}

    auto operator*() {
        return std::string(get(), ' ');
    }
    int get() const {return i-tabsize;}
    int depth() const {return i/tabsize;}

    int &i;
};

inline auto print_containerv(auto prefix, auto&&container, auto tostring) {
    printv("{:}", prefix);
    for(auto const&c: container)
        printv("{:} ", tostring(c));
    printlnv("");
}
inline auto print_id_containerv(auto prefix, auto&&container) {
    print_containerv(prefix, container, [](auto const&id){return id.string();});
}

inline auto print_link_containerv(auto prefix, auto&&container) {
    print_containerv(prefix, container, [](auto const&l){return l.first.string();});
}
