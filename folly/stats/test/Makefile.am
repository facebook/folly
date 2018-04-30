ACLOCAL_AMFLAGS = -I m4

CPPFLAGS = -I$(top_srcdir)/test/gtest/googletest/include
ldadd = $(top_builddir)/test/libfollytestmain.la

TESTS = \
				buffered_stat_test \
				digest_builder_test \
				histogram_test \
				quantile_estimator_test \
				sliding_window_test \
				tdigest_test

check_PROGRAMS = $(TESTS)

buffered_stat_test_SOURCES = BufferedStatTest.cpp
buffered_stat_test_LDADD = $(ldadd)

digest_builder_test_SOURCES = DigestBuilderTest.cpp
digest_builder_test_LDADD = $(ldadd)

histogram_test_SOURCES = HistogramTest.cpp
histogram_test_LDADD = $(ldadd)

quantile_estimator_test_SOURCES = QuantileEstimatorTest.cpp
quantile_estimator_test_LDADD = $(ldadd)

sliding_window_test_SOURCES = SlidingWindowTest.cpp
sliding_window_test_LDADD = $(ldadd)

tdigest_test_SOURCES = TDigestTest.cpp
tdigest_test_LDADD = $(ldadd)
