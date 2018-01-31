find_package(PkgConfig)

if (UNIX)
    if (USE_GTK2)
        pkg_check_modules(Gtk REQUIRED IMPORTED_TARGET gtk+-2.0 gtk+-x11-2.0)
    else()
        pkg_check_modules(Gtk REQUIRED IMPORTED_TARGET gtk+-3.0)
    endif()
endif()

if (UNIX)
    add_subdirectory(IMdkit)
    add_subdirectory(gtk-im)
    add_subdirectory(qt5-im)
endif()

set(SRCS ../common/main.c ../common/im.c
    ../common/bihua.c ../common/common.c ../common/s2t.c ../common/nl.c  ../common/english.c
    ../common/dict.c ../common/keyboard.c ../common/mapfile.c  ../common/aes.c ../common/book.c
    ../common/keytool.c  ../common/keydesc.c
    ../common/select.c ../common/layout.c ../common/translate.c  ../common/ybus.c
    ../common/menu.c ../common/history.c ../common/replace.c
    ybus-xim.c ybus-lcall.c ui.c lcall.c)

add_executable(yong ${SRCS})
target_compile_definitions(yong PRIVATE -DL_CALL_GLIB_SERVER)
target_compile_options(yong PRIVATE -I${CMAKE_CURRENT_SOURCE_DIR} 
    -I${CMAKE_CURRENT_SOURCE_DIR}/IMdkit)


if (UNIX)
    set(CMAKE_SKIP_RPATH true)
    target_link_libraries(yong PRIVATE -Wl,-rpath,'$ORIGIN' l)
    target_link_libraries(yong PRIVATE dl m Ximd X11 PkgConfig::Gtk)
endif()

if (TOOL_BIHUA)
    add_executable(bihua ../common/bihua.c ../common/mapfile.c)
    target_compile_options(bihua PRIVATE -I${PROJECT_SOURCE_DIR}/include 
        -I${PROJECT_SOURCE_DIR}/llib -Wall -DTOOLS_BIHUA)
endif()

install(TARGETS yong DESTINATION ${YONG_PRIVATE_LIBDIR})
