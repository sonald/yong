file(GLOB SRCS LIST_DIRECTORIES false *.c)
list(APPEND SRCS ../common/trie.c ../common/pinyin.c)

add_library(mb SHARED ${SRCS})
target_include_directories(mb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
if (UNIX)
    target_compile_options(mb PRIVATE -fvisibility=hidden)
    target_link_libraries(mb PRIVATE dl l pthread)
endif()


install(TARGETS mb DESTINATION ${YONG_PRIVATE_LIBDIR})
