SUBDIRS = .

lib_LTLIBRARIES = libfollylogging.la

libfollylogging_la_SOURCES = \
	LogCategory.cpp \
	Logger.cpp \
	LoggerDB.cpp \
	LogHandler.cpp \
	LogLevel.cpp \
	LogMessage.cpp \
	LogName.cpp \
	LogStream.cpp \
	LogStreamProcessor.cpp \
	xlog.cpp

libfollylogging_la_LIBADD = $(top_builddir)/libfolly.la
libfollylogging_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION)
