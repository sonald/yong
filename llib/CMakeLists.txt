file(GLOB SRCS LIST_DIRECTORIES false *.c)

add_library(l SHARED ${SRCS})
target_include_directories(l INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(l PRIVATE -fpic)

install(TARGETS l DESTINATION ${YONG_PRIVATE_LIBDIR})


