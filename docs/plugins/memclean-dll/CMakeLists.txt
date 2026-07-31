cmake_minimum_required(VERSION 3.15)
project(memclean C)

# memclean.c holds the cleaning itself, plugin.c exposes it through the
# four symbols the host expects.
add_library(memclean SHARED memclean.c plugin.c)
target_compile_definitions(memclean PRIVATE MEMCLEAN_EXPORTS UNICODE _UNICODE)

if(MSVC)
    target_compile_options(memclean PRIVATE /W4 /utf-8)
endif()
