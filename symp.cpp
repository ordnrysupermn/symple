#include <filesystem>
import std;

#include <cstdlib>

#include "build.h"
#include "conan.h"
#include "config.h"
#include "cpp.h"
#include "defs.h"
#include "environment.h"
#include "textfile.h"

namespace options {

bool verbose = false;
bool compile_verbose = false;
bool dependency_verbose = false;
std::filesystem::path build_log = defaults::build_log;

} // options

auto append_containers(auto &a, auto &&b) {
    a.insert(std::end(a), std::begin(b), std::end(b));
    return a;
}

namespace filesystem {

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
static constexpr auto make_extension_filter(auto extension) {
    return [extension](auto const& e) {return e.path().extension() == extension && e.is_regular_file();};
}
static constexpr auto make_extensions_filter(auto &&extensions) {
    return [extensions](auto const& e) {return extensions.find(e.path().extension()) != std::end(extensions);};
}
struct directory {
    using files = std::list<std::filesystem::path>;
    auto find_file(auto name) {
        auto find_file = [&](auto const&e){return name.filename() == e.path().filename() && e.is_regular_file();};
        files res{std::from_range,
                std::filesystem::recursive_directory_iterator(this->root, std::filesystem::directory_options::skip_permission_denied)
                | std::views::filter(find_file)
                | std::views::transform(&std::filesystem::directory_entry::path)
            };
        return res;
    }
    auto find_files2(auto ext) {
        files fs{std::from_range,
                std::filesystem::recursive_directory_iterator(this->root, std::filesystem::directory_options::skip_permission_denied)
                | std::views::filter(no_symp_dir)
                | std::views::filter(make_extensions_filter(ext))
                | std::views::transform(&std::filesystem::directory_entry::path)
            };
        return fs;
    }
    auto find_files(auto ext) {
        files fs{std::from_range,
                std::filesystem::recursive_directory_iterator(this->root, std::filesystem::directory_options::skip_permission_denied)
                | std::views::filter(no_symp_dir)
                | std::views::filter(make_extension_filter(ext))
                | std::views::transform(&std::filesystem::directory_entry::path)
            };
        return fs;
    }
    auto remove() {
        if(std::filesystem::exists(this->root))
            std::filesystem::remove_all(this->root);
        return true;
     }
    std::filesystem::path root;
};

struct build_directory : directory {
    auto find_cmake_files() {return find_files(defaults::cmake_extension);}

};
struct project_directory : directory {
    auto find_sources() {return find_files2(std::set<std::string>{defaults::cpp_extension, defaults::module_extension});}
    auto find_modules() {return find_files(defaults::module_extension);}
};
struct data_directory : directory {
};

} // filesystem

struct project {
    auto get_data_directory() const {return this->data_directory.root;}
    auto get_build_directory() const {return this->build_directory.root;}

    auto create_build_directory() {
        auto d = get_build_directory();
        printlnv("Creating build directory: {:}", d.string());
        std::filesystem::create_directories(d);
        options::build_log = d/defaults::build_log;
        printlnv("Build log is: {:}", options::build_log.string());
        return true;
    }

    auto remove_data_directory() {
        printlnv("Removing data directory: {:}", this->data_directory.root.string());
        return this->data_directory.remove();
    }
    auto remove_build_directory() {
        printlnv("Removing build directory: {:}", this->build_directory.root.string());
        return this->build_directory.remove();
    }

    using main_symbols = std::list<std::filesystem::path>;
    static auto read_main_symbol(std::filesystem::path p) {
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
        // TODO: probably there is a better way to identify it as this will fail
        // in a broken compilation being cleaned up
        // maybe the binary shall live in .symp and just sym-linked to the root
        auto f = get_build_directory()/defaults::main_detect;
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

    static auto is_object(auto p) {
        return (p.extension() == defaults::cpp_extension);
    }
    static auto is_module(auto p) {
        return (p.extension() == defaults::module_extension);
    }

    auto compile_object(auto const&c, auto &x, auto source) {
        auto object = get_build_directory()/source.filename();
        object.replace_extension(defaults::object_extension);
        printlnv2("    Adding compile command {:}", object.string());
        x.add_build_command(object, [source, object, &c](){return c.compile_cpp(source, object);});
        x.add_dependency(object, source);
        return object;
    }
    auto precompile_module(auto const&c, auto&x, auto source) {
        auto pcm = get_build_directory()/source.filename().replace_extension(defaults::precompiled_module_extension);
        printlnv2("    Adding precompile command {:}", pcm.string());
        x.add_build_command(pcm, [source, pcm, &c]() {return c.precompile_module(source, pcm);});
        x.add_dependency(pcm, source);
        return pcm;
    }
    auto compile_deps_file(auto const&c, auto&x, auto source, auto object) {
        auto deps = get_build_directory()/source;
        deps.replace_extension(defaults::dependency_extension);
        printlnv2("    Adding deps build: {:}", deps.string());
        x.add_build_command(deps, [source, deps, &c]() {
                return c.create_dependencies(source, deps);
                }, [object, deps, &x]() {
                        x.add_dependency(object, environment::compiler::read_deps_file(deps));
                        return EXIT_SUCCESS;
                    });
        x.add_dependency(deps, source);
        return deps;
    }

    auto compile(environment::compiler &c) {
        const auto start = std::chrono::steady_clock::now();
        build::executor x;
        auto &conan_db = this->conan_db;
        auto build_directory =  get_build_directory();

        auto sources = this->directory.find_sources();
        std::unordered_map<build::id, build::id> modules;
        std::unordered_map<build::id, conan::package_header> packages;
        std::set<build::id> objects;
        auto conanfile = build_directory/defaults::conanfile;
        auto main_detect = build_directory/defaults::main_detect;

        print_id_containerv("Sources: ", sources);

        while(sources.size()) {
            auto source = sources.front();
            sources.pop_front();
            // create source build
            printlnv2("Processing {:}", source.string());
            auto object = compile_object(c, x, source);
            x.add_dependency(main_detect, object);
            objects.insert(object);

            build::id pcm;
            if(is_module(source)) {
                pcm = precompile_module(c, x, source);
                x.add_dependency(object, pcm);
            }

            // TODO: this scanning shall only be re-done if the file was changed, which
            // means that it has to have an output file where we save the dependencies, similar to the .deps
            // file, and then we just need to sum those files up
            // in this version we will always scan all the files, but it is not optimal
            auto pds = cpp::scan_package_dependency(source, defaults::modules_search_depth);

            printlnv2("    Dependencies:");
            for(auto m: pds.modules) {
                auto module_source = textfile::append_extension(m, defaults::module_extension);
                auto ps = this->directory.find_file(module_source);
                bool external = false;
                // module was not found locally, search in compiler path
                if(ps.empty()) {
                    ps = c.find_module(module_source);
                    external = true;
                }
                // TODO: if the module not found, we shall probably search in conan for it as well
                if(ps.empty()) {
                    std::println("Could not find module source {:}", module_source.string());
                    return false;
                }
                if(ps.size() > 1) {
                    std::println("Ambiguous module path:");
                    for(auto const&p: ps)
                        std::println("{:}", p.string());
                    std::println("Using {:}", ps.front().string());
                }
                module_source = ps.front();
                auto dpcm = build_directory/textfile::append_extension(m, defaults::precompiled_module_extension);
                printlnv2("        Adding dependent module: {:} -> {:}", object.string(), dpcm.string());
                x.add_dependency(object, dpcm);

                if(is_module(source)) {
                    printlnv2("        Adding dependent module: {:} -> {:}", pcm.string(), dpcm.string());
                    x.add_dependency(pcm, dpcm);
                }
                if(external) {
                    auto [ignored, inserted] = modules.insert({module_source, dpcm});
                    if(inserted)
                        sources.push_back(module_source);
                }
            }
            // BUG: external modules compile to their folder!
            
            // collect required conan packages
            // NOTE: if we find any conan packages, we have to add the conanfile as dependency to the deps file
            // to ensure that we have downloaded the packages before the compiler checks for it
            bool has_conan = false;
            for(auto i: pds.includes) {
                auto p = conan_db.get_package_from_include(i);
                if(!p || p->ignored())
                    continue;
                printlnv2("        Adding dependent package: {:} -> [{:}, {:}]", object.string(), p->name, i);
                packages.insert({i, *p});
                has_conan = true;
            }
            if(has_conan)
                x.add_dependency(conanfile, source);

            // NOTE: dependency building seems to not work with pcm, need to further investigate
            if(is_object(source)) {
                auto deps = compile_deps_file(c, x, source, object);
                x.add_dependency(object, deps);
                if(has_conan)
                    x.add_dependency(deps, conanfile);
            }

        }
        if(!modules.empty())
            print_link_containerv("External modules: ", modules);

        if(!packages.empty())
            print_link_containerv("Packages: ", packages);
        std::unordered_set<build::id> conanfiles;
        for(auto [i, h]: packages) {
            printlnv2("Processing: {:}", i.string());
            auto conan = build_directory/textfile::append_extension(i, defaults::conan_extension);
            printlnv2("    Adding conan package: {:}", conan.string());
            x.add_build_command(conan, [h, conan, &conan_db]() {return conan_db.get_package_info(h.name, conan);});
            x.add_dependency(conanfile, conan);
            conanfiles.insert(conan);
        }
        std::unordered_set<std::string> package_names{std::from_range, packages 
                | std::views::transform([&](auto const&p){return p.second.name;})
            };
        if(!conanfiles.empty()) {
            x.add_build_command(conanfile, [build_directory, conanfile, conanfiles, &conan_db](){
                    std::list<conan::reference> references;
                    for(auto const&c: conanfiles) {
                        auto pi = conan_db.read_package_info(c);
                        if(pi) {
                            printlnv2("    Using reference: {:}", pi->reference());
                            references.push_back(pi->reference());
                        }
                    }         
                    conan_db.generate_conanfile(conanfile, references); 
                    return conan_db.install_packages(build_directory, options::build_log);
                }, [this, package_names, &conan_db, &c]() {
                    auto files = this->build_directory.find_cmake_files();
                    std::tie(c.project_includes, c.project_libraries, c.project_library_archives) = conan_db.read_packages_compilation_data(files, package_names);
                    return EXIT_SUCCESS;
                });
        }
        
        x.add_build_command(main_detect, [main_detect, build_directory](){
                return std::system(std::format("nm --defined-only --print-file-name {:}/*.o | grep -i \"t\\s[_]*main\" {:}", build_directory.string(), environment::os::redirect_to(main_detect)).c_str());
            }, [this, main_detect, &objects, &c, &x](){
                auto mains = read_main_symbol(main_detect);
                if(mains.empty()) {
                    std::println("Could not detect main symbol");
                    return EXIT_FAILURE;
                }
                if(mains.size() > 1) {
                    std::println("More than one main symbol found:");
                    for(auto const&p: mains)
                        std::println("{:}", p.string());
                    return EXIT_FAILURE;
                }           
                auto main = mains.front().stem();
                x.add_build_command(main, [main, &objects, &c]() {
                        return c.link(main, objects, c.project_library_archives);
                    });
                x.add_dependency(main, main_detect);
                return EXIT_SUCCESS;
            });
        // NOTE: we have to delay the scheduling of main as we do not know main at this point
        // so just re-kick the build later
        auto r = x.build();
        if(r)
            return false; 

        const auto finish = std::chrono::steady_clock::now();
        printlnv("Took {:} milliseconds", std::chrono::duration_cast<std::chrono::milliseconds>(finish - start));

        return (r == EXIT_SUCCESS);
    }
    filesystem::project_directory directory;
    filesystem::data_directory data_directory{directory.root/defaults::directory};
    filesystem::build_directory build_directory{directory.root/defaults::directory/defaults::build_directory};
    conan::db conan_db;
};


int main(int argc, char* argv[]) {
    std::filesystem::path root = ".";
    project p{{root}};
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
            if(s == "wipe") {
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
            if(s == "--verbose2" || s == "-vv") {
                options::verbose = true;
                options::dependency_verbose = true;
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

