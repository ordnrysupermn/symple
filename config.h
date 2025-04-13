#pragma once

import std;

namespace defaults {
    constexpr auto std_standard = "c++2b";
    constexpr auto std_lib = "libc++";
    constexpr auto warnings = "all";

    constexpr auto module_extension = ".cppm";
    constexpr auto precompiled_module_extension = ".pcm";
    constexpr auto cpp_extension = ".cpp";
    constexpr auto object_extension = ".o";

    constexpr auto directory = ".symp";
    constexpr auto build_directory = "build";
    constexpr std::size_t max_cat_lines = 20;
    constexpr std::size_t modules_search_depth = 50;
    constexpr auto build_log = "build.log";
    constexpr auto compiler_test_file = "t.test";
    constexpr auto compiler_info_file = "compiler_info";
    constexpr auto compiler_details_file = "compiler_details";
    constexpr auto dependency_extension = ".deps";
    constexpr auto main_detect = "main_detect";

    constexpr auto conan_extension = ".conan";
    constexpr auto cmake_extension = ".cmake";
    constexpr auto conanfile = "conanfile.txt";
    constexpr auto conan_library_folder = "lib";
    constexpr auto conan_include_folder = "include";

    // TODO: dirty hack, no space support
    constexpr auto path_regex = "([^\\s\\\\]+)";
};


