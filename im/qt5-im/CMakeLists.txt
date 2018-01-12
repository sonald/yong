file(GLOB SRCS LIST_DIRECTORIES false *.cpp)
list(APPEND SRCS ../lcall.c)

set(CMAKE_AUTOMOC on)
pkg_check_modules(GLib REQUIRED IMPORTED_TARGET glib-2.0)
find_package(Qt5Gui COMPONENTS Private)

add_library(yongplatforminputcontextplugin SHARED ${SRCS})
target_compile_options(yongplatforminputcontextplugin PRIVATE -I${PROJECT_SOURCE_DIR}/llib
    -DL_CALL_GLIB_CLIENT -fvisibility=hidden -I${PROJECT_SOURCE_DIR}/im)
target_include_directories(yongplatforminputcontextplugin PRIVATE
    ${Qt5Gui_PRIVATE_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})
target_link_libraries(yongplatforminputcontextplugin PRIVATE 
    -Wl,--no-undefined -Wl,-O1 -Wl,-z,relro
    Qt5::Gui PkgConfig::GLib dl)

install(TARGETS yongplatforminputcontextplugin DESTINATION ${YONG_PRIVATE_LIBDIR}/qt5-im)
