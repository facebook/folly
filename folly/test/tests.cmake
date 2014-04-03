# add test executable target
add_executable(bitstest folly/test/BitsTest.cpp)

# dep on gtest
add_dependencies(bitstest googletest gtest gtest_main)
add_dependencies(bitstest libfolly libfolly_benchmark)
target_link_libraries(bitstest libfolly libfolly_benchmark ${GTEST_LIBRARY})
#target_link_libraries(bitstest gtest)

# make make test work
add_test(NAME bitstest COMMAND bitstest)