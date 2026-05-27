#pragma once

#include <cstdlib>

import std;

#include "defs.h"
#include "textfile.h"

namespace build {

using id = std::filesystem::path;
using ids = std::set<id>;

struct dependency_graph {
    using required_ids = std::unordered_map<id, ids>;
    auto add(auto id, auto &&dep) {
        auto &rs = this->required[id];
        std::ranges::copy(std::begin(dep), std::end(dep), std::inserter(rs, std::begin(rs)));
    }
    required_ids required;
};

struct command {
    using lambda = std::function<int()>;

    // keep the result of the last completion
    int complete_result = EXIT_SUCCESS;

    auto execute() {
        this->done = false;
        this->complete_result = EXIT_SUCCESS;
        return main();
    }

    auto complete() {
        if (this->done)
            return this->complete_result;
        this->done = true;
        if (!post)
            return this->complete_result;
        this->complete_result = post();
        return this->complete_result;
    }

    lambda main;
    lambda post;
    bool done = false;
};

struct target {
    id i;
    command c;
};


struct executor {
    auto add_build_command(auto id, auto main) {
        this->targets[id] = {main};
    }
    auto add_build_command(auto id, auto main, auto post) {
        this->targets[id] = {main, post};
    }
    auto add_dependency(auto id, build::id dep) {
        this->dependencies.add(id, std::list<build::id>{dep});
    }
    auto add_dependency(auto id, auto &&deps) {
        this->dependencies.add(id, deps);
    }

    auto needs_build(auto id) {
        static int indent = 0;
        indenter in(indent);
        printlnv2("{:} checking: {:}", *in, id.string());
        auto i = this->targets.find(id);
        if(i == std::end(this->targets)) {
            printlnv2("{:} no target: NO", *in);
            return false;
        }
        if(!std::filesystem::exists(id)) {
            printlnv2("{:} no file: YES", *in);
            return true;
        }
        auto time = std::filesystem::last_write_time(id);
        printlnv2("{:} last write of {:} is {:}", *in, id.string(), time);
        auto j = this->dependencies.required.find(id);
        if(j == std::end(this->dependencies.required)) {
            printlnv2("{:} No dependency: NO", *in);
            return false;
        }
        auto k = std::ranges::find_if(j->second, [&] (auto const&d) {
                auto dependent_time = std::filesystem::last_write_time(d);
                printlnv2("{:} last write of {:} is {:}", *in, d.string(), time);
                if(dependent_time > time) {
                    printlnv2("{:} Time {:} > {:}:  YES", *in, dependent_time, time);
                    return true;
                }
                return needs_build(d);
            });
        auto r = (k != std::end(j->second));
        if(r)
            printlnv2("{:} dependency needs build: YES", *in);
        else
            printlnv2("{:} all dependency met: NO", *in);
        return r;
    }
    auto all_ready(auto const&ids) {
        auto i = std::ranges::find_if(ids, [&](auto const&v) {
                auto t = this->targets.find(v);
                if(t == std::end(this->targets)) {
                    printlnv2("Not found in targets: {:}", v.string());
                    return false;
                }
                printlnv2("Done: {:} {:}", t->first.string(), t->second.done);
                return !t->second.done;
            });
        return (i == std::end(ids));
    }
    auto can_be_built(auto id) {
        auto d = this->dependencies.required.find(id);
        if(d == std::end(this->dependencies.required))
            return true;
        return all_ready(d->second);
    }
    auto build(auto &v) {
        if(options::interrupted)
            return EXIT_FAILURE;
        printlnv2("Selected: {:}", v.first.string());
        if(needs_build(v.first)) {
            printlnv2("Building: {:}", v.first.string());
            // NOTE: ensure that the directory structure exists
            auto parent_path = std::filesystem::weakly_canonical(v.first.parent_path());
            std::filesystem::create_directories(parent_path);
            auto r = v.second.execute();
            if(r) {
                textfile::cat(options::build_log);
                return r;
            }
            if(options::interrupted)
                return EXIT_FAILURE;
        }
        printlnv2("Completing {:}", v.first.string());
        if(options::interrupted)
            return EXIT_FAILURE;
        int cr = v.second.complete();
        if (cr != EXIT_SUCCESS) {
           if (interrupted) {
            return EXIT_FAILURE; // Do not print the error message
        }
        std::cout << "Post build step failed (code {:}): {:}" 
                  << cr << v.first.string() << std::endl;
        return cr; // propagate the real code
        }
        return EXIT_SUCCESS;
    }
    auto check_all_done() {
        auto t = std::ranges::find_if(this->targets, [](auto const&v) {
                return !v.second.done;
                });
        if(t == std::end(this->targets)) {
            printlnv2("Compilation done!");
            return EXIT_SUCCESS;
        }

        std::println("Unbuilt targets found:");
        for(auto const&v: this->targets) {
            if(!v.second.done)
                std::println("{:}", v.first.string());
        }
        return  EXIT_FAILURE;
    }
    auto build() {
        static build::id last;
        for(;;) {
            auto t = std::ranges::find_if(this->targets, [&](auto &v) {
                    printlnv2("Can be built check: {:}", v.first.string());
                    if(!can_be_built(v.first)) {
                        printlnv2("Target cannot be built");
                        return false;
                    }
                    return !v.second.done;
                });
            if(t == std::end(this->targets))
                return check_all_done();
            if(last == t->first) {
                std::println("Circular reference, potential endless loop");
                return EXIT_FAILURE;
            }
            last = t->first;
            // BEWARE: we are using the unordered_map invalidation rules to allow adding further dependency
            // and targets.
            auto r = build(*t);
            if(r)
                return r;

        }
        return EXIT_SUCCESS;
    }

    std::unordered_map<id, command> targets;
    dependency_graph dependencies;
};

} // build
