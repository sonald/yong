file(GLOB SRCS LIST_DIRECTORIES false *.c)

add_executable(yong-vim ${SRCS})

install(TARGETS yong-vim DESTINATION ${YONG_PRIVATE_LIBDIR})
