file(GLOB SRCS LIST_DIRECTORIES false *.c)
list(APPEND SRCS ../common/trie.c)

add_library(cloud SHARED ${SRCS})
target_link_libraries(cloud l)

install(TARGETS cloud DESTINATION ${YONG_PRIVATE_LIBDIR})
