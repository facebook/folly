ACLOCAL_AMFLAGS = -I m4

CPPFLAGS = -I$(top_srcdir)/test/gtest/googletest/include
ldadd = $(top_builddir)/test/libfollytestmain.la

check_PROGRAMS = \
	conv_test

TESTS = $(check_PROGRAMS)

conv_test_SOURCES = ConvTest.cpp
conv_test_LDADD = $(ldadd)
