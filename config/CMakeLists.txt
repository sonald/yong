find_package(PkgConfig)

set(SRCS config_ui.c config_main.c sync.c gz.c session.c aes.c update.c)
list(APPEND SRCS ../common/translate.c ../common/trie.c)

if (WIN32)
    list(APPEND SRCS config_win.c lpng.c)
elseif (UNIX)
    list(APPEND SRCS config_gtk.c)
    if (USE_GTK2)
        pkg_check_modules(Gtk REQUIRED IMPORTED_TARGET gtk+-2.0)
    else()
        pkg_check_modules(Gtk REQUIRED IMPORTED_TARGET gtk+-3.0)
    endif()
endif()

add_executable(yong-config ${SRCS})
target_compile_definitions(yong-config PRIVATE -DCFG_CUSTOM_XML)
if (WIN32)
    #cr('yong-config.rc','yong-config.res','$(WINDRES) -i $^ -o $@ -O coff');
    #OBJS.push("yong-config.res");
    #env("LDFLAGS+=-mwindows");
    target_link_libraries(yong-config l gdi32 comctl32 comdlg32 ws2_32 ole32 uuid)
else()
    if (USE_GTK2)
        list(APPEND LIBS glib-2.0 gobject-2.0 cairo X11 pango-1.0 pangocairo-1.0 gdk_pixbuf-2.0 gdk-x11-2.0
            gtk-x11-2.0)
    endif()
    target_link_libraries(yong-config -Wl,-rpath,'$ORIGIN' l PkgConfig::Gtk ${LIBS})
endif()


#if(target=="xml") {
	#bin2c("custom.xml","custom.c",{static:true,zero:true,name:"config_custom"});
	#bin2c("sync.xml","custom_sync.c",{static:true,zero:true,name:"config_sync"});
	#bin2c("update.xml","custom_update.c",{static:true,zero:true,name:"config_update"});
#}

install(TARGETS yong-config DESTINATION ${YONG_PRIVATE_LIBDIR})
