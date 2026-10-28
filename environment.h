#pragma once

import std;

#include "build.h"
#include "compile.h"
#include "config.h"
#include "defs.h"
#include "textfile.h"

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
        << compile::std_standard(defaults::std_standard);
        for(auto const&f: this->dependency_flags)
            c << compile::custom(f);
        c << compile::includes(this->project_includes)
        << compile::custom("-MM -MF")
        << compile::custom(o.string()) << compile::custom(i.string());

        printlnv("Creating dependencies for file: {:}: {:}", i.string(), c.get());
        return c.spawn(get_build_log());
    }
    auto compile_cpp(std::filesystem::path i, std::filesystem::path o) const {
        compile::command c;
        c << compile::compiler(this->name) 
        << compile::std_standard(defaults::std_standard)
        << compile::warnings(defaults::warnings) 
        << compile::verbose(options::compile_verbose) 
        << compile::compile_only()
        << compile::includes(this->project_includes);
        for(auto const&f: this->compile_flags)
            c << compile::custom(f);
        c << compile::custom(i.string()) 
        << compile::archive(o);

        printlnv("Compiling: {:}: {:}", i.string(), c.get());
        return c.spawn(get_build_log());
    }
    auto precompile_module(std::filesystem::path i, std::filesystem::path o) const {
        
        compile::command c;
        c << compile::compiler(this->name) 
        << compile::std_standard(defaults::std_standard);
        if(i.filename() == "std.cc"&&this->module_artifact_extension == ".gcm") {
            c << compile::custom("-fmodules")
            << compile::custom("-fsearch-include-path")
            << compile::compile_only()
            << compile::custom("bits/std.cc");
            return c.spawn(get_build_log());
        }
        
        c << compile::warnings(defaults::warnings)
        << compile::verbose(options::compile_verbose);
        for(auto const&f: this->precompile_flags)
            c << compile::custom(f);
        c << compile::custom(i.string());
        if(this->module_precompile_output)
            c << compile::archive(o);

        printlnv("Compiling: {:}: {:}", i.string(), c.get());
        return c.spawn(get_build_log());
    }

    auto link(std::filesystem::path t, auto const&objects, auto const&libraries) const {
        compile::command c;
        c << compile::compiler(this->name);
        for(auto const&f: this->link_flags)
            c << compile::custom(f);
        c << compile::libraries(this->project_libraries);
        for(auto const&o: objects)
            c << compile::custom(o);
        c << compile::archive(t);
        for(auto const&l: libraries)
            c << compile::library(l);

        printlnv("Linking: {:}: {:}", t.string(), c.get());
        return c.spawn(options::build_log);
    }

    using dependecy_files = std::list<std::filesystem::path>;
    static auto read_deps_file(std::filesystem::path p) {
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

    auto find_module(std::filesystem::path p) {
        auto find_file = [&](auto const&e){return p.filename() == e.path().filename() && e.is_regular_file();};
        std::list<std::filesystem::path> res;
        if(p.filename() == "std.cppm"&&this->module_artifact_extension == ".gcm") {
            for(auto const&d: this->hlist) {
                auto candidate = d/"bits"/"std.cc";
                if(std::filesystem::is_regular_file(candidate)) {
                    res.push_back(candidate);
                    return res;
                }
            }
        }
        auto search_directory = [&](auto const&directory, bool recursive) {
            if(!std::filesystem::is_directory(directory))
                return;
            auto candidate = directory/p.filename();
            if(std::filesystem::is_regular_file(candidate))
                res.push_back(std::move(candidate));
            if(!recursive)
                return;
            auto files = std::filesystem::recursive_directory_iterator(
                    directory, std::filesystem::directory_options::skip_permission_denied)
                | std::views::filter(find_file)
                | std::views::transform(&std::filesystem::directory_entry::path);
            std::ranges::copy(files, std::back_inserter(res));
        };
        for(auto const&directory: this->hlist) {
            auto is_cpp_include = directory.filename().string().starts_with("c++");
            search_directory(directory, is_cpp_include);
        }
        if(!this->modules_directory.empty() && std::filesystem::exists(this->modules_directory)) {
            search_directory(this->modules_directory, true);
        }
        res.sort();
        res.unique();
        return res;
    }

    auto module_artifact_path(std::filesystem::path module) const {
        if(module.filename() == "std.cc"&&this->module_artifact_extension == ".gcm")
            return this->module_artifact_directory/"std.gcm";
        if(module.extension() == defaults::module_extension)
            module.replace_extension(this->module_artifact_extension);
        else
            module += this->module_artifact_extension;
        return this->module_artifact_directory/module;
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
        std::vector<std::string> compile_flags;
        std::vector<std::string> link_flags;
        std::vector<std::string> precompile_flags;
        std::vector<std::string> dependency_flags; 
        std::string module_artifact_extension = defaults::precompiled_module_extension;
        std::filesystem::path module_artifact_directory;
        bool module_precompile_output = true;
        bool native_std_module = false;
        std::string std_module_compile_flag;
        std::filesystem::path install_directory;
        std::filesystem::path modules_directory;
        using include_directories = std::set<std::filesystem::path>;
        include_directories hlist;
        include_directories qlist;
        std::filesystem::path build_directory;
        include_directories project_includes;
        using library_directories = std::set<std::filesystem::path>;
        library_directories project_libraries;
        using library_archives = std::set<std::string>;
        library_archives project_library_archives;
};

using available_compilers = std::list<compiler>;
available_compilers detect_compilers(std::filesystem::path build_directory);

} // environment
