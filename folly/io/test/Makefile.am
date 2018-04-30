ACLOCAL_AMFLAGS = -I m4

CPPFLAGS = -I$(top_srcdir)/test/gtest/googletest/include
ldadd = $(top_builddir)/test/libfollytestmain.la

# compression_test takes several minutes, so it's not run automatically.
TESTS = \
	iobuf_test \
	iobuf_cursor_test \
	iobuf_queue_test \
	record_io_test \
	shutdown_socket_set_test

check_PROGRAMS = $(TESTS) \
		 compression_test

iobuf_test_SOURCES = IOBufTest.cpp
iobuf_test_LDADD = $(ldadd)

iobuf_cursor_test_SOURCES = IOBufCursorTest.cpp
iobuf_cursor_test_LDADD = $(ldadd)

compression_test_SOURCES = ../../compression/test/CompressionTest.cpp
compression_test_LDADD = $(top_builddir)/libfolly.la \
			 $(top_builddir)/test/libgtest.la \
			 $(top_builddir)/libfollybenchmark.la

iobuf_queue_test_SOURCES = IOBufQueueTest.cpp
iobuf_queue_test_LDADD = $(ldadd)

record_io_test_SOURCES = RecordIOTest.cpp
record_io_test_LDADD = $(ldadd)

shutdown_socket_set_test_SOURCES = ShutdownSocketSetTest.cpp
shutdown_socket_set_test_LDADD = $(ldadd)
