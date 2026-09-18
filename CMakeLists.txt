cmake_minimum_required(VERSION 3.10)
project(deadbeef-cui C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fPIC")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# DeaDBeeF plugin builder usually sets these manually if no pkg-config is available
# Since we know where deadbeef.h is (/usr/include/deadbeef), we can just use it directly
include_directories(/usr/include)

add_library(cui SHARED 
    src/main.c
    src/cui_widget.c
    src/cui_data.c
    src/cui_scriptable.c
)
# PREFIX "" + OUTPUT_NAME: the build emits ddb_misc_cui_GTK3.so directly, the
# filename DeaDBeeF requires (the _load entry-point symbol is derived from the
# plugin filename) and the name the plugin-builder manifests list in "out".
# C_VISIBILITY_PRESET hidden: exactly one symbol (ddb_misc_cui_GTK3_load,
# marked default-visibility in main.c) is exported; every internal global
# stays out of the player's dynamic symbol space. Verified with
# nm -D build/ddb_misc_cui_GTK3.so. Applied to the plugin target only — the
# test executable defines its own main.c globals in the mock.
set_target_properties(cui PROPERTIES PREFIX "" OUTPUT_NAME "ddb_misc_cui_GTK3" C_VISIBILITY_PRESET hidden)

target_include_directories(cui PRIVATE ${GTK3_INCLUDE_DIRS})
target_link_libraries(cui PRIVATE ${GTK3_LIBRARIES} ${CMAKE_DL_LIBS})
target_compile_options(cui PRIVATE ${GTK3_CFLAGS_OTHER})

# Test suite (GLib GTest). Off by default so a plain build stays plugin-only.
#   cmake -S . -B build -DBUILD_TESTS=ON && cmake --build build
#   ctest --test-dir build --output-on-failure
option(BUILD_TESTS "Build the cui test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_executable(cui_tests
        tests/test_cui.c
        tests/mock_deadbeef.c
        src/cui_data.c
        src/cui_widget.c
        src/cui_scriptable.c
    )
    target_include_directories(cui_tests PRIVATE src tests ${GTK3_INCLUDE_DIRS})
    target_link_libraries(cui_tests PRIVATE ${GTK3_LIBRARIES} ${CMAKE_DL_LIBS})
    target_compile_options(cui_tests PRIVATE ${GTK3_CFLAGS_OTHER})
    add_test(NAME cui_tests COMMAND cui_tests)
endif()
