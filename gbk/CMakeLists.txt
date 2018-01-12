file(GLOB SRCS LIST_DIRECTORIES false *.c)

add_library(gbk SHARED ${SRCS})
target_link_libraries(gbk l)

install(TARGETS gbk DESTINATION ${YONG_PRIVATE_LIBDIR})
