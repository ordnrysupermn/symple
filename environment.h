#pragma once

import std;

#include "config.h"
#include "compile.h"
#include "tools.h"

namespace environment {

struct os {
    static auto redirect_to(auto const&p) {return std::string(">" + p.string() + " 2>&1");}
};

struct compiler {
    auto get_build_log() const {return this->build_directory / defaults::build_log;}

    // INTENTIONALLY TAKING THE LOG
    auto invoke(std::filesystem::path build_log, auto &&...args) {
        compile::command c;
        c << compile::compiler(this->name) << compile::compile_only();
        (void)(c << ... <<args);
        printlnv("Compiler invoked: {:}", c.value);
        return c.spawn(build_log);
    }

    auto create_dependencies(std::filesystem::path i, std::filesystem::path o) const {
        compile::command c;
        c << compile::compiler(this->name)
            << compile::includes(this->project_includes)
            << compile::custom("-MM -MF")
            << compile::custom(o.string()) << compile::custom(i.string());
        printlnv("Creating dependencies for file: {:}: {:}", i.string(), c.get());
        return c.spawn(get_build_log());
    }
    auto compile_cpp(std::filesystem::path i, std::filesystem::path o) const {
        compile::command c;
        c << compile::compiler(this->name) << compile::std_standard(defaults::std_standard) << compile::std_lib(defaults::std_lib)
                << compile::warnings(defaults::warnings)  << compile::verbose(options::compile_verbose) << compile::compile_only()
                << compile::includes(this->project_includes)
                << compile::custom(std::string("-fprebuilt-module-path=") + build_directory.string())
                << compile::custom(i.string()) << compile::archive(o);

        printlnv("Compiling: {:}: {:}", i.string(), c.get());
        return c.spawn(get_build_log());
    }
    auto precompile_module(std::filesystem::path i, std::filesystem::path o) const {
        compile::command c;
        c << compile::compiler(this->name) << compile::std_standard(defaults::std_standard) << compile::std_lib(defaults::std_lib)
                << compile::warnings(defaults::warnings)  << compile::verbose(options::compile_verbose) << compile::compile_only()
                << compile::custom("--precompile")
                << compile::custom(i.string()) << compile::archive(o);


        printlnv("Compiling: {:}: {:}", i.string(), c.get());
        return c.spawn(get_build_log());
    }

    auto link(std::filesystem::path t, std::list<std::filesystem::path> objects) const {
        compile::command c;
        c << compile::compiler(this->name);
        for(auto const&o: objects)
            c << compile::custom(o);
        c << compile::archive(t);
        printlnv("Linking: {:}: {:}", t.string(), c.get());
        return c.spawn(options::build_log);
    }

    using dependecy_files = std::list<std::filesystem::path>;
    auto read_deps_file(std::filesystem::path p) {
        dependecy_files fs;
        std::ifstream is(p);

        for(auto const&l: std::ranges::subrange(textfile::in_it(is), textfile::in_it())){
            static std::regex regex(defaults::path_regex); 
            std::smatch m;
            std::regex_search(l, m, regex);
            for(auto const&m: std::ranges::subrange(std::sregex_iterator(std::begin(l), std::end(l), regex), std::sregex_iterator()) | std::views::drop(1)) {
                fs.push_back(m.str());
            };
        }
        return fs;
    }

    auto search_modules(std::filesystem::path p) {
        auto find_file = [&](auto const&e){return p.filename() == e.path().filename() && e.is_regular_file();};
        std::list<std::filesystem::path> res{std::from_range,
                std::filesystem::recursive_directory_iterator(this->modules_directory, std::filesystem::directory_options::skip_permission_denied)
                | std::views::filter(find_file)
                | std::views::transform(&std::filesystem::directory_entry::path)
            };
        return res;
    }

    auto search_includes(std::filesystem::path p) {
        auto make_range = [](auto d){return std::ranges::subrange(std::filesystem::directory_iterator{d}, std::filesystem::directory_iterator());};
        auto find_file = [&](auto const&e){return p.filename() == e.path().filename() && e.is_regular_file();};
        printlnv("Searching for file: {:}", p.string());
        std::list<std::filesystem::path> res{std::from_range, this->hlist
                | std::views::transform(make_range)
                | std::views::join
                | std::views::filter(find_file)
            };
        return res;
    }

    std::string name;
    std::filesystem::path install_directory;
    std::filesystem::path modules_directory;
    using include_directories = std::list<std::filesystem::path>;
    include_directories hlist;
    include_directories qlist;
    std::filesystem::path build_directory;
    include_directories project_includes;
};

using available_compilers = std::list<compiler>;
available_compilers detect_compilers(std::filesystem::path build_directory);

} // environment
