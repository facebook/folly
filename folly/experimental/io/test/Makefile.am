ACLOCAL_AMFLAGS = -I m4

CPPFLAGS = -I$(top_srcdir)/test/gtest/googletest/include
ldadd = $(top_builddir)/test/libfollytestmain.la

check_PROGRAMS = \
	fs_util_test

TESTS = $(check_PROGRAMS)

fs_util_test_SOURCES = FsUtilTest.cpp
fs_util_test_LDADD = $(ldadd)
