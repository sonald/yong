cmake_minimum_required(VERSION 2.8)
project(yong)

option(TOOL_BIHUA "build tool bihua" OFF)
option(USE_GTK2 "use gtk2 to build ui" OFF)


# common flags used by all targets
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -fno-strict-aliasing -pipe")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -g -Og -fno-strict-aliasing -pipe")
if (UNIX)
    add_compile_options(-Wl,--hash-style=sysv)
endif()

include(GNUInstallDirs)

set(YONG_INSTALL_DATADIR ${CMAKE_INSTALL_DATADIR}/yong)
if (UNIX)
    if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
        set(YONG_PRIVATE_LIBDIR ${YONG_INSTALL_DATADIR}/l64)
    else()
        set(YONG_PRIVATE_LIBDIR ${YONG_INSTALL_DATADIR}/l32)
    endif()
else()
    if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
        set(YONG_PRIVATE_LIBDIR ${YONG_INSTALL_DATADIR}/w64)
    else()
        set(YONG_PRIVATE_LIBDIR ${YONG_INSTALL_DATADIR}/w32)
    endif()
endif()

include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/common)

#todo: build multi lib
add_subdirectory(llib)
add_subdirectory(gbk)
add_subdirectory(cloud)
add_subdirectory(config)
add_subdirectory(im)
add_subdirectory(mb)
add_subdirectory(vim)

find_package(Gettext)
if (GETTEXT_FOUND)
    gettext_process_po_files(zh_CN ALL PO_FILES im/gtk-im/zh_CN.po)
    install(FILES ${PROJECT_BINARY_DIR}/zh_CN.gmo RENAME zh_CN.mo DESTINATION ${YONG_INSTALL_DATADIR}/locale)
endif()

# installation rules
install(FILES im/yong-tool.sh 
    DESTINATION ${YONG_INSTALL_DATADIR} 
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ)
install(FILES im/yong.xml im/yong.ini data/README.txt data/yong.chm 
    data/keyboard.ini data/bihua.bin data/normal.txt
    DESTINATION ${YONG_INSTALL_DATADIR})
install(DIRECTORY im/skin DESTINATION ${YONG_INSTALL_DATADIR})
install(DIRECTORY data/mb DESTINATION ${YONG_INSTALL_DATADIR}
    FILES_MATCHING PATTERN "*.*")
install(DIRECTORY data/entry DESTINATION ${YONG_INSTALL_DATADIR}
    FILES_MATCHING PATTERN "*.*")

