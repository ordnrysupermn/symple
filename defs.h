#pragma once

#include "config.h"

#define printv(...) do{if(options::verbose)std::print(__VA_ARGS__);}while(false)
#define printlnv(...) do{if(options::verbose)std::println(__VA_ARGS__);}while(false)

namespace options {
    static bool verbose = false;
    static bool compile_verbose = false;
    static std::filesystem::path build_log = defaults::build_log;
};




