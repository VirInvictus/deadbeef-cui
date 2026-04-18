cmake_minimum_required(VERSION 3.10)
project(deadbeef-cui C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fPIC")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# DeaDBeeF plugin builder usually sets these manually if no pkg-config is available
# Since we know where deadbeef.h is (/usr/include/deadbeef), we can just use it directly
include_directories(/usr/include)

add_library(cui SHARED src/main.c)
set_target_properties(cui PROPERTIES PREFIX "") # so it compiles to cui.so instead of libcui.so

target_include_directories(cui PRIVATE ${GTK3_INCLUDE_DIRS})
target_link_libraries(cui PRIVATE ${GTK3_LIBRARIES} ${CMAKE_DL_LIBS})
target_compile_options(cui PRIVATE ${GTK3_CFLAGS_OTHER})
