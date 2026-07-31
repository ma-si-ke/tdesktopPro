cmake_minimum_required(VERSION 3.15)
project(memclean C)

add_library(memclean SHARED memclean.c)
target_compile_definitions(memclean PRIVATE MEMCLEAN_EXPORTS UNICODE _UNICODE)

if(MSVC)
    target_compile_options(memclean PRIVATE /W4 /utf-8)
endif()

add_executable(memclean_test test.c)
target_link_libraries(memclean_test PRIVATE memclean)
target_compile_definitions(memclean_test PRIVATE UNICODE _UNICODE)
