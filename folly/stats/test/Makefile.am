ACLOCAL_AMFLAGS = -I m4

CPPFLAGS = -I$(top_srcdir)/test/gtest/googletest/include
ldadd = $(top_builddir)/test/libfollytestmain.la

TESTS = \
				histogram_test \
				tdigest_test

TESTS = $(check_PROGRAMS)

histogram_test_SOURCES = HistogramTest.cpp
histogram_test_LDADD = $(ldadd)

tdigest_test_SOURCES = TDigestTest.cpp
tdigest_test_LDADD = $(ldadd)
