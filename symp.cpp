import std;

#include <cstdlib>

#include "build.h"
#include "conan.h"
#include "config.h"
#include "cpp.h"
#include "defs.h"
#include "environment.h"
#include "textfile.h"

using namespace compile;
struct project {
    auto get_data_directory() const {return this->root / defaults::directory ;}
    auto get_build_directory() const {return this->root / defaults::directory / defaults::build_directory;}

    auto create_build_directory() {
        auto d = get_data_directory();
        if(!std::filesystem::exists(d)) {
            printlnv("Creating data directory: {:}", d.string());
            if(!std::filesystem::create_directory(d))
                return false;
        }
        d = get_build_directory();
        if(!std::filesystem::exists(d)) {
            printlnv("Creating build directory: {:}", d.string());
            if(!std::filesystem::create_directory(d))
                return false;
        }
        options::build_log = d / defaults::build_log;
        printlnv("Build log is: {:}", options::build_log.string());
        return true;
    }

    auto remove_data_directory() {
        const auto d = get_data_directory();
        printlnv("Removing data directory: {:}", d.string());
        if(std::filesystem::exists(d))
            std::filesystem::remove_all(d);
        return true;
     }

    auto remove_build_directory() {
        const auto d = get_build_directory();
        printlnv("Removing build directory: {:}", d.string());
        if(std::filesystem::exists(d))
            std::filesystem::remove_all(d);
        return true;
     }

    static constexpr auto make_extension_filter(auto extension) {
        return [extension](auto const& e) {return e.path().extension() == extension && e.is_regular_file();};
    }

    using compilation_units = std::list<compilation_unit>;

    auto collect_compilation_units() {
        auto no_symp_dir = [](auto const&e) {
            if(!e.is_directory())
                return true;
            auto p = std::filesystem::canonical(e.path());
            do {
                if(p.filename() == defaults::directory)
                    return false;
                p = p.parent_path();
            } while(p != p.parent_path());
            return true;
        };
        auto create_compilation_unit = [](auto const&p) {
            return compilation_unit{p};};
        // get the compilation units of the filesystem
        compilation_units us{std::from_range,
                std::filesystem::recursive_directory_iterator(this->root, std::filesystem::directory_options::skip_permission_denied)
                | std::views::filter(no_symp_dir)
                | std::views::filter(make_extension_filter(defaults::cpp_extension))
                | std::views::transform(&std::filesystem::directory_entry::path)
                | std::views::transform(create_compilation_unit)
            };
        return us;
    }
    using build_targets = std::list<build::target>;
    auto create_dependency_build_targets(environment::compiler &c, compilation_units &cus) {
        build_targets dts;
        std::ranges::transform(cus, std::back_inserter(dts), [&](auto &u) {
                auto input = u.path;
                auto output = c.build_directory / u.path;
                output.replace_extension(defaults::dependency_extension);
                build::target t{output, [c, input, output]() {return c.create_dependencies(input, output);}};
                t.dependencies.push_back({input});
                return t;
            });
        return dts;
    }
    auto create_compilation_unit_build_targets(environment::compiler &c, compilation_units &cus) {
        build_targets cuts;
        std::ranges::transform(cus, std::back_inserter(cuts), [&](auto &u) {
                auto i = u.path;
                auto o = c.build_directory / u.path;
                o.replace_extension(defaults::object_extension);
                auto deps = o;
                deps.replace_extension(defaults::dependency_extension);
                build::target t{o, [c, i, o]() {return c.compile_cpp(i, o);}, {{deps}}};
                return t;
            });
        return cuts;
    }
    auto read_file_dependecies(environment::compiler &c, build_targets &cuts) {
        for(auto &t: cuts) {
            auto deps = c.read_deps_file(t.dependencies.front().output);
            std::ranges::transform(std::begin(deps), std::end(deps), std::back_inserter(t.dependencies), [](auto const&p) {
                        return build::target{p};
                    });
         }
    }

    // TODO: this scanning shall only be re-done if the file was changed, which
    // means that it has to have an output file where we save the dependencies, similar to the .deps
    // file, and then we just need to sum those files up
    // in this version we will always scan all the files, but it is not optimal
    auto scan_package_dependencies(auto const&compilation_units) {
        std::list<cpp::package_dependency> data{std::from_range, compilation_units 
                | std::views::transform([](auto const &u) {
                        return cpp::scan_package_dependency(u.input(), defaults::modules_search_depth);
                })
            };
        return data;
    }

    auto create_conan_build_targets(auto const&conan_db, auto const&package_dependencies) {
        std::unordered_set<std::string> is{std::from_range, package_dependencies 
                | std::views::transform([](auto const&d){return d.includes;})
                | std::views::join
            };
        build_targets cnts;
        for(auto const&i: is) {
            auto p = conan_db.get_package_from_include(i);
            if(!p) {
                printlnv("Cound not find package for include: {:}", i);
                continue;
            }
            if(p->ignored()) {
                printlnv("Ignored include: {:}", i);
                continue;
            }
            auto o = get_build_directory() / append_extension(p->name, defaults::conan_extension);
            cnts.push_back(build::target{o, [o, p, &conan_db]() {
                return conan_db.get_package_info(p->name, o);}});
        }
        return cnts;
    }
    auto read_package_info(auto const&cnts) {
        std::list<std::filesystem::path> outputs{std::from_range, cnts
                    | std::views::transform([&](auto const&t) {return t.output;})};
        std::list<conan::package_info> packages;
        for(auto const&o: outputs) {
            auto pi = conan_db.read_package_info(o);
            if(pi)
                packages.push_back(*pi);
        }
        return packages;
    }
    auto read_packages_compilation_data(auto const&conan_db, auto const&packages) {
        std::list<std::string> include_prefixes{std::from_range, packages 
                | std::views::transform([&](auto const&p){return p.name;})
            };
        std::set<std::filesystem::path> includes;
        std::set<std::filesystem::path> libraries;
        auto d = get_build_directory();

        for(auto &p: std::ranges::subrange(std::filesystem::recursive_directory_iterator(d, std::filesystem::directory_options::skip_permission_denied), std::filesystem::recursive_directory_iterator())
                | std::views::filter(make_extension_filter(defaults::cmake_extension))
                | std::views::transform(&std::filesystem::directory_entry::path)
           ) {
            auto fs = conan_db.get_release_folders(p, include_prefixes);
            auto is = conan_db.get_release_includes(fs, include_prefixes);
            auto ls = conan_db.get_release_libraries(fs, include_prefixes);
            std::ranges::copy(is, std::inserter(includes, std::begin(includes)));
            std::ranges::copy(ls, std::inserter(libraries, std::begin(libraries)));
        }
        
        std::set<std::string> archives;
        for(auto const&p: packages) {
            if(conan_db.is_library(p.name))
                archives.insert(p.name);
        }
        return std::make_tuple(includes, libraries, archives);
    }
    auto create_conan_install_target(environment::compiler &c, auto const&conan_db, auto const&packages) {
        std::list<conan::reference> references{std::from_range, packages 
            | std::views::transform(&conan::package_info::reference)};
        auto o = get_build_directory() / defaults::conanfile;
        auto d = get_build_directory();
        build::target cmt{o, [o, d, references, &conan_db](){
                conan_db.generate_conanfile(o, references);
                return conan_db.install_packages(d, options::build_log);
             }};
        return cmt;
    }

    auto create_module_build_targets(environment::compiler &c, auto const&package_dependencies) {
        
        std::unordered_set<std::filesystem::path> ms{std::from_range, package_dependencies 
                | std::views::transform([](auto const&d){return d.modules;})
                | std::views::join
            };
        build_targets mts{std::from_range, ms 
                | std::views::transform([&](auto m) {
                    auto module_source = append_extension(m, defaults::module_extension);
                    auto ps = c.search_modules(module_source);
                    auto o = get_build_directory() / append_extension(m, defaults::precompiled_module_extension);
                    if(ps.empty())
                        return build::target{o, [module_source]() {
                                std::println("Could not find module source {:}", module_source.string());
                                return EXIT_FAILURE;
                            }}; 
                    if(ps.size() > 1) {
                        std::println("Ambiguous module path:");
                        for(auto const&p: ps)
                            std::println("{:}", p.string());
                        std::println("Using {:}", ps.front().string());
                    }
                    auto i = ps.front();
                    return build::target{o, [c, i, o]() {return c.precompile_module(i, o);}, {{i}}};
                })
            };
        return mts;
    }

    using main_symbols = std::list<std::filesystem::path>;
    auto read_main_symbol(std::filesystem::path p) {
        main_symbols ms;
        std::ifstream is(p);
        for(auto const&l: std::ranges::subrange(textfile::in_it(is), textfile::in_it())){
            static std::regex regex(std::string("^") + defaults::path_regex + ":.*"); 
            std::smatch m;
            std::regex_match(l, m, regex);
            if(m.size() == 2)
                ms.push_back(m[1].str());
        }
        return ms;
    }

    auto remove_binary() {
        auto f = get_build_directory() / defaults::main_detect;
        if(std::filesystem::exists(f)) {
            auto mains = read_main_symbol(f);

            for(auto const&m: mains) {
                // ignore errors
                std::error_code ec;
                std::filesystem::remove(m.stem(), ec);
            }
        }
        
        return false;
    }

    auto create_main_detect_target() {
        auto f = get_build_directory() / defaults::main_detect;
        build::target mdt{f, [this, f]() {
                return std::system((std::string("nm --defined-only --print-file-name ") + get_build_directory().string()
                    + "/*.o | grep -i \"t\s[_]*main\" " + environment::os::redirect_to(f)).c_str());
            }};
        // if we already have the file, add the contents as dependency so the build gets linked to it
        if(std::filesystem::exists(f)) {
            auto mains = read_main_symbol(f);
            // we have one real main
            if(mains.size() == 1) {
                mdt.dependencies.push_back({mains.front()});
            }
        }
        return mdt;
    }

    auto create_main_target(environment::compiler &c, build_targets const&cuts, build::target const&mdt) {
        auto mains = read_main_symbol(mdt.output);
        if(mains.empty()) {
            return build::target{"", []() {
                    std::println("No main symbol found in the project");
                    return 1;
                }};
        }
        if(mains.size() > 1) {
            return build::target{"", [mains]() {
                printv("Multiple main symbols found: ");
                for(auto const&m: mains)
                    printv("{:}", m.string());
                printlnv();
                return 1;
            }};
        }
        auto main = mains.front().stem();
        std::list<std::filesystem::path> objs{std::from_range, cuts 
                | std::views::transform([](auto const&t){return t.output;})};
        return build::target{main, [c, main, objs]() {return c.link(main, objs, c.project_library_archives);}, cuts};
    }

    auto build(build_targets::value_type &t) {
        auto r = t.build_if_needed();
        if(r) {
            textfile::cat(options::build_log);
            return false;
        }
        return true;
    }
    auto build(build_targets &ts) {
        for(auto &t: ts) {
            if(!build(t))
                return false;
        }
        return true;
    }

    auto compile(environment::compiler &c) {
        auto cus = collect_compilation_units();

        auto pds = scan_package_dependencies(cus);
        auto cnts = create_conan_build_targets(this->conan_db, pds);
        if(!build(cnts))
            return false;

        auto packages = read_package_info(cnts);

        auto cts = create_conan_install_target(c, this->conan_db, packages);
        if(!build(cts))
            return false;
        std::tie(c.project_includes, c.project_libraries, c.project_library_archives) = read_packages_compilation_data(this->conan_db, packages);

        auto dts = create_dependency_build_targets(c, cus);
        if(!build(dts))
            return false;

       auto mts = create_module_build_targets(c, pds);
        if(!build(mts))
            return false;

        auto cuts = create_compilation_unit_build_targets(c, cus);
        read_file_dependecies(c, cuts);
         if(!build(cuts))
            return false;

        auto mdt = create_main_detect_target();
        if(!build(mdt))
            return false;

        auto main = create_main_target(c, cuts, mdt);

        if(!build(main))
            return false;

        return true;
    }
    std::filesystem::path root;
    conan::db conan_db;
};


int main(int argc, char* argv[]) {
    std::filesystem::path root = ".";
    project p{root};
    bool build = true;

    if(argc > 1) {
        for(auto s: std::span(argv, argc)
                | std::views::drop(1)
                | std::views::transform([](auto v){return std::string_view(v);})) {
            if(s == "clean") {
                printlnv("Project cleanup is executed");
                p.remove_binary();
                p.remove_build_directory();
                continue;
            }
            if(s == "--no-build") {
                printlnv("No build is requested");
                build = false;
                continue;
            }
            if(s == "--wipe") {
                printlnv("Project wipe is executed");
                p.remove_binary();
                p.remove_build_directory();
                p.remove_data_directory();
                build = false;
                continue;
            }
            if(s == "--verbose" || s == "-v") {
                options::verbose = true;
                continue;
            }
            if(s == "--compile_verbose") {
                options::compile_verbose = true;
                continue;
            }
        }
    }
    if(!build)
        return EXIT_SUCCESS;

    if(!p.create_build_directory()) {
        std::println("Cannot create build directory");
        return EXIT_FAILURE;
    }

    auto cs = environment::detect_compilers(p.get_build_directory());
    if(cs.empty()) {
        std::println("Failed to detect any valid compiler");
        return EXIT_FAILURE;
    }

    // TODO select compiler
    auto c = cs.front();
    c.build_directory = p.get_build_directory();
    printlnv("Detected compiler is: {:}, installed to: {:}", c.name, c.install_directory.string());
    auto print_includes = [](std::string_view pfx, auto const&is) {
            if(is.empty())
                return;
            printlnv("Includes:");
            // NOTE: this does not work with the current clang std
            // printlnv(std::runtime_format(pfx));
            for(auto i: is)
                printlnv("{:}", i.string());
        };
    print_includes("Q includes", c.qlist);
    print_includes("H includes", c.hlist);

    return (p.compile(c) ? EXIT_SUCCESS : EXIT_FAILURE);
}

