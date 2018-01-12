file(GLOB SRCS LIST_DIRECTORIES false *.c)
list(APPEND SRCS ../lcall.c)

add_library(im-yong SHARED ${SRCS})
set_target_properties(im-yong PROPERTIES PREFIX "")
target_compile_options(im-yong PRIVATE -I${PROJECT_SOURCE_DIR}/llib
    -DL_CALL_GLIB_CLIENT -fvisibility=hidden -I${PROJECT_SOURCE_DIR}/im)
if ("$ENV{GTK}" STREQUAL "gtk2")
    list(APPEND LIBS glib-2.0 gobject-2.0 cairo pango-1.0 pangocairo-1.0 gdk_pixbuf-2.0 gdk-x11-2.0
        gtk-x11-2.0)
endif()
target_link_libraries(im-yong PkgConfig::Gtk X11 ${LIBS})


install(TARGETS im-yong DESTINATION ${YONG_PRIVATE_LIBDIR}/gtk-im)
