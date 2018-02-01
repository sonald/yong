file(GLOB SRCS LIST_DIRECTORIES false *.cpp)

find_package(PkgConfig)
pkg_check_modules(GLib REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(PY REQUIRED IMPORTED_TARGET libpinyin)

add_library(py SHARED ${SRCS})
target_include_directories(py PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(py PRIVATE ../llib /usr/include/libpinyin-1.3.0)
target_compile_options(py PRIVATE -fvisibility=hidden -std=c++11)
target_link_libraries(py PRIVATE dl l PkgConfig::PY PkgConfig::GLib)


install(TARGETS py DESTINATION ${YONG_PRIVATE_LIBDIR})

