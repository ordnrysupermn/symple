#include <regex>
import std;
#include "config.h"
#include "environment.h"

namespace environment {

struct compiler_clang {
    auto scan_details(std::filesystem::path p) {
        compiler::include_directories qlist;
        compiler::include_directories hlist;

        static std::regex include_regex(std::string("^\\s*") + defaults::path_regex + "\\s*.*");
        auto convert_and_add = [&](auto const&l, auto &list){
                std::smatch m;
                std::regex_match(l, m, include_regex);
                if(m.size() != 2)
                    printlnv("Unrecognized include path: {:}", l.string());
                else
                    list.push_back(std::filesystem::weakly_canonical({m[1]}));
            };
        // TODO: with full ranges this code is just totally broken, i assume it has something with the istream interface
        // and ranges collaboration, probably needs further investigation
        std::ifstream is(p);
        std::regex qregex(".*include.*\"\\.\\.\\.\".*starts.*");
        auto q = std::ranges::find_if(textfile::in_it(is), textfile::in_it(), [&](auto const&l){return std::regex_match(l.string(), qregex);});
        if(q == textfile::in_it()) {
            printlnv("Could not locate search list in compiler output");
            return std::make_tuple(qlist, hlist);
        }
        std::regex hregex(".*include.*<\\.\\.\\.>.*starts.*");
        auto h = std::ranges::find_if(textfile::in_it(is), textfile::in_it(), [&](auto const&l){
                    auto match = std::regex_match(l.string(), hregex);
                    if(!match)
                        convert_and_add(l, qlist);
                    return match;
                });
        if(h == textfile::in_it()) {
            printlnv("Could not locate hlist in compiler output");
            return std::make_tuple(qlist, hlist);
        }
        std::regex eregex(".*End.*search.*");
        auto e = std::ranges::find_if(textfile::in_it(is), textfile::in_it(), [&](auto const&l){
                    auto match = std::regex_match(l.string(), eregex);
                    if(!match)
                        convert_and_add(l, hlist);
                    return match;
                });
        return std::make_tuple(qlist, hlist);
    }

    auto scan_info(std::filesystem::path p) {
        std::ifstream is(p);
        std::regex bindir_regex(std::string("InstalledDir:\\s*") + defaults::path_regex + "\\s*.*");
        std::smatch m;
        auto i = std::ranges::find_if(textfile::in_it(is), textfile::in_it(), [&](auto const&l) {
                    return std::regex_match(l, m, bindir_regex);
                });
        if(m.size() != 2) {
            printlnv("Could not locate installation directory");
            return std::make_tuple(std::filesystem::path{}, std::filesystem::path{});
        }
        std::filesystem::path id{std::filesystem::path{m[1]}.parent_path()};
        std::filesystem::path md = id / "share";

        return std::make_tuple(id, md);
    }


    auto detect(std::filesystem::path build_directory) -> std::expected<compiler, std::string> {
        compiler c{"clang++"};

        auto info_file = build_directory / defaults::compiler_info_file;
        build::target compiler_info_target{info_file, [&](){
                return std::system((c.name + " --version " + os::redirect_to(info_file)).c_str());
            }};
        auto r = compiler_info_target.build_if_needed();
        if(WEXITSTATUS(r)) {
            printlnv("Compiler binary {:} not found", c.name);
            return std::unexpected("Failed to detect binary");
        }
        std::tie(c.install_directory, c.modules_directory) = scan_info(info_file);

        auto test_file = build_directory / defaults::compiler_test_file;
        auto test_output_file = test_file;
        test_output_file.replace_extension(defaults::object_extension);        
        auto details_file = build_directory / defaults::compiler_details_file;
        build::target compiler_test_target{test_file, [&]() {
                return std::system((std::string("touch ") + test_file.string() + os::redirect_to(details_file)).c_str());
            }};

        r = compiler_test_target.build_if_needed();
        if(WEXITSTATUS(r)) {
            printlnv("Cannot create compiler test file {:}", test_file.string());
            return std::unexpected("Cannot create test file");
        }
        build::target compiler_details_target{details_file, [&]() {
                return c.invoke(details_file, compile::custom("-x c++"),
                    compile::verbose(true), compile::custom(test_file), compile::archive(test_output_file));
            }, {compiler_test_target}};

        r = compiler_details_target.build_if_needed();
        if(WEXITSTATUS(r)) {
            printlnv("Compiler command returned an error, assuming invalid");
            return std::unexpected("Failed to detect compiler details");
        }
        std::tie(c.qlist, c.hlist) = scan_details(details_file);
        return c;
    }
};

using supported_compilers = std::tuple<compiler_clang>;

available_compilers detect_compilers(std::filesystem::path build_directory) {
    available_compilers cs;
    std::apply([&](auto c) {
            auto r = c.detect(build_directory);
            if(r)
                cs.push_back(*r);
        }, supported_compilers{});
    return cs;
}

} // environment
