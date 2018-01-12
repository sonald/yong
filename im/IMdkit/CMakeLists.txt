file(GLOB SRCS LIST_DIRECTORIES false *.c)

add_library(Ximd STATIC ${SRCS})
target_include_directories(Ximd PRIVATE ../../llib)
target_include_directories(Ximd INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
