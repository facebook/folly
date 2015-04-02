check_PROGRAMS = mcrouter_fibers_test

mcrouter_fibers_test_SOURCES = \
  FibersTest.cpp \
  main.cpp

mcrouter_fibers_test_CPPFLAGS = -I$(top_srcdir)/oss_include
mcrouter_fibers_test_LDADD = $(top_builddir)/lib/libmcrouter.a -lgtest -lfollybenchmark
