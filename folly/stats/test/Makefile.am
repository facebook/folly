ACLOCAL_AMFLAGS = -I m4

CPPFLAGS = -I$(top_srcdir)/test/gtest/googletest/include
ldadd = $(top_builddir)/test/libfollytestmain.la

TESTS = \
				histogram_test \
				sliding_window_test \
				tdigest_test

check_PROGRAMS = $(TESTS)

histogram_test_SOURCES = HistogramTest.cpp
histogram_test_LDADD = $(ldadd)

sliding_window_test_SOURCES = SlidingWindowTest.cpp
sliding_window_test_LDADD = $(ldadd)

tdigest_test_SOURCES = TDigestTest.cpp
tdigest_test_LDADD = $(ldadd)
