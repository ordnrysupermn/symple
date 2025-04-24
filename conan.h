#pragma once

#include <cstdlib>
import std;

#include "environment.h"

namespace conan {

struct package_header {
    using header_files = std::unordered_set<std::string>;

    auto contains_include(std::string include) const -> bool {
        auto i = std::ranges::find(this->headers, include);
        return (i != std::end(this->headers));
    }
    bool ignored() const {return (this->package_type == IGNORE);}
    bool library() const {return (this->package_type == LIBRARY);}
    bool header_only() const {return (this->package_type == HEADER_ONLY);}

    std::string name;
    header_files headers;
    enum {
        HEADER_ONLY,
        LIBRARY,
        IGNORE,
    } package_type = HEADER_ONLY;
    std::string cmake_target = name;
};

using reference = std::string;

struct package_info {
    using version = std::string;

    auto reference() const {return std::format("{:}/{:}", this->name, this->latest);}

    std::string name;
    version latest;
    std::filesystem::path include;
};

struct db {
    using package_data = std::vector<package_header>;

    auto is_library(auto name) const {
        auto i = std::ranges::find_if(this->headers, [&](auto const&h) {return h.name == name;});
        if(i != std::end(this->headers))
            return i->library();
        return false;
    }
    auto get_package_from_include(std::string include) const -> std::optional<conan::package_header> {
        auto i = std::ranges::find_if(this->headers, [&](auto const&h) {return h.contains_include(include);});
        if(i != std::end(this->headers))
            return *i;
        return {};
    }

    auto get_package_info(auto package_name, auto info_file) const {
        auto cmd = std::format("conan search {:} {:}"
                        , package_name
                        , environment::os::redirect_to(info_file)
                    );
        printlnv("Retriving package info: {:}", cmd);
        return std::system(cmd.c_str());
    }
    auto generate_conanfile(auto file, auto const&references) const {
        std::ofstream os(file);
        os << "[requires]\n";
        for(auto const&r: references)
            os << r << "\n";
        os << "[generators]\n"
            << "CMakeDeps\n"
            << "CMakeToolchain" << std::endl;
    }
    auto read_package_info(auto info_file) const -> std::expected<package_info, std::string> {
        package_info pi;
        std::ifstream is(info_file);

        for(auto const&l: std::ranges::subrange(textfile::in_it(is), textfile::in_it())){
            static std::regex regex("\\s*(.*)/(.*)"); 
            std::smatch m;
            std::regex_match(l, m, regex);
            if(m.size() == 3) {
                pi.name = m[1];
                pi.latest = m[2];
            }
        }
        if(pi.latest.empty())
            return std::unexpected("Could not retrieve version information");
        return pi;
    }
    auto get_release_folders(auto info_file, auto const&references) const {
        std::list<std::regex> regexes{std::from_range, references
                | std::views::transform([](auto const&r) {
                        return std::regex{std::format(".*\\({:}_PACKAGE_FOLDER_RELEASE\\s+\"(.*)\"\\)", r)};
            })};
        std::set<std::filesystem::path> folders;
        textfile::scan_lines(info_file, regexes, [&](auto const&m){
                if(m.size() == 2)
                    folders.insert(std::filesystem::path(m[1]));
            });
        return folders;
    }
    auto get_release_includes(auto const&folders, auto const&references) const {
        std::set<std::filesystem::path> includes;
        std::ranges::transform(folders, std::inserter(includes, std::begin(includes)), [](auto const&p) {return p / defaults::conan_include_folder;}); 
        return includes; 
    }
    auto get_release_libraries(auto const&folders, auto const&references) const {
        std::set<std::filesystem::path> libraries;
        std::ranges::transform(folders, std::inserter(libraries, std::begin(libraries)), [](auto const&p) {return p / defaults::conan_library_folder;}); 
        return libraries; 
    }
    auto get_package_cmake_target(auto name) const {
        auto i = std::ranges::find_if(headers, [&](auto const&p) {return p.name == name;});
        if(i == std::end(this->headers))
                return std::string{};
        return i->cmake_target;
    }
    auto read_packages_compilation_data(auto const&files, auto const&package_names) {
        std::set<std::filesystem::path> includes;
        std::set<std::filesystem::path> libraries;

        for(auto const&p: files) {
            auto fs = get_release_folders(p, package_names);
            auto is = get_release_includes(fs, package_names);
            auto ls = get_release_libraries(fs, package_names);
            std::ranges::copy(is, std::inserter(includes, std::begin(includes)));
            std::ranges::copy(ls, std::inserter(libraries, std::begin(libraries)));
        }
        
        std::set<std::string> archives;
        for(auto const&n: package_names) {
            if(is_library(n))
                archives.insert(get_package_cmake_target(n));
        }
        return std::make_tuple(includes, libraries, archives);
    }
    auto install_packages(auto const&build_directory, auto build_log) const {
        // NOTE: this needs to be generated with our internal compiler settings
        printlnv("Generating conan profile");
        auto r = std::system(std::format("conan profile detect --force {:}", environment::os::redirect_to(build_log)).c_str());
        if(r != EXIT_SUCCESS)
            return r;
        auto cmd = std::format("conan install {:} --output-folder={:} --build=missing {:}"
                        , build_directory.string()
                        , build_directory.string()
                        , environment::os::redirect_to(build_log)
                    );
        printlnv("Installing conan packages: {:}", cmd);
        return std::system(cmd.c_str());
    }
    
    package_data headers = {
            {"cpp-httplib", {"httplib/httplib.h"}, package_header::HEADER_ONLY, {"httplib"}},
            {"ncurses", {"ncurses.h"}, package_header::LIBRARY},
            {"cstdlib", {"cstdlib", "cstdlib.h"}, package_header::IGNORE},
            {"zlib", {"zlib.h"}, package_header::LIBRARY, {"z"}},
        };
};

} // conan
