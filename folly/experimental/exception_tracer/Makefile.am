SUBDIRS = .

lib_LTLIBRARIES = libfollyexception_tracer.la

libfollyexception_tracer_la_SOURCES = \
	ExceptionCounterLib.cpp \
	ExceptionStackTraceLib.cpp \
	ExceptionTracer.cpp \
	ExceptionTracerLib.cpp \
	StackTrace.cpp

libfollyexception_tracer_la_LIBADD = $(top_builddir)/libfolly.la $(top_builddir)/experimental/symbolizer/libfollysymbolizer.la
libfollyexception_tracer_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION)
