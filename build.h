#pragma once

import std;
#include "defs.h"

namespace build {

struct target {
    auto needs_build() const {
        if(!std::filesystem::exists(this->output))
            return true;
        if(this->dependencies.empty())
            return false;
        auto time = std::filesystem::last_write_time(this->output);
        auto i = std::ranges::find_if(this->dependencies, [time](auto const&d) {
                if(d.needs_build())
                    return true;
                
                auto dependent_time = std::filesystem::last_write_time(d.output);
                return dependent_time > time; 
            });
        return (i != std::end(this->dependencies));
    }

    auto build() {
        for(auto &d: this->dependencies) {
            d.build_if_needed();
        }
        if(!this->command) {
            std::println("No build command for {:}", this->output.string());
            return EXIT_FAILURE;
        }
        return this->command();
    }

    auto build_if_needed() -> int {
       if(needs_build()) {
            printlnv("Building {:}", this->output.string());
            return build();
       }
       return 0;
    }

    std::filesystem::path output;
    using build_command = std::function<int()>;
    build_command command;
    using target_dependencies = std::list<target>;
    target_dependencies dependencies;
};

} // build
