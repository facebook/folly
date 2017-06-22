noinst_PROGRAMS = logging_example
noinst_LTLIBRARIES = libfollylogging_example.la

logging_example_SOURCES = main.cpp
logging_example_LDADD = libfollylogging_example.la

libfollylogging_example_la_SOURCES = lib.cpp
libfollylogging_example_la_LIBADD = \
	$(top_builddir)/experimental/logging/libfollylogging.la
