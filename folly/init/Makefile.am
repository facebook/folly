SUBDIRS = .

lib_LTLIBRARIES = libfollyinit.la

libfollyinit_la_SOURCES = Init.cpp
libfollyinit_la_LIBADD = $(top_builddir)/libfolly.la
libfollyinit_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION)
