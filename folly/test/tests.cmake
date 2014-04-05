# Tests that do not require folly_libbenchmark
set(BASIC_TESTS Portability CpuId MapUtil GroupVarint Traits)

foreach(t ${BASIC_TESTS})
    add_executable(${t}Test folly/test/${t}Test.cpp)
    add_dependencies(${t}Test googletest gtest gtest_main libfolly)
    target_link_libraries(${t}Test libfolly ${GTEST_LIBRARY})

    # make test work
    add_test(NAME ${t}Test COMMAND ${t}Test)
endforeach()

# Tests that do not require folly_libbenchmark

# Tests with some other requirements
# add test executable target
# add_executable(bitstest folly/test/BitsTest.cpp)

# dep on gtest
# add_dependencies(bitstest googletest gtest gtest_main)
# add_dependencies(bitstest libfolly libfolly_benchmark)
# target_link_libraries(bitstest libfolly libfolly_benchmark ${GTEST_LIBRARY})

# make make test work
# add_test(NAME bitstest COMMAND bitstest)