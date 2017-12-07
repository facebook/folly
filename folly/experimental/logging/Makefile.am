SUBDIRS = . example

lib_LTLIBRARIES = libfollylogging.la

libfollylogging_la_SOURCES = \
	AsyncFileWriter.cpp \
	FileHandlerFactory.cpp \
	FileWriterFactory.cpp \
	GlogStyleFormatter.cpp \
	ImmediateFileWriter.cpp \
	Init.cpp \
	LogCategory.cpp \
	LogCategoryConfig.cpp \
	LogConfig.cpp \
	LogConfigParser.cpp \
	Logger.cpp \
	LoggerDB.cpp \
	LogHandlerConfig.cpp \
	LogLevel.cpp \
	LogMessage.cpp \
	LogName.cpp \
	LogStream.cpp \
	LogStreamProcessor.cpp \
	printf.cpp \
	RateLimiter.cpp \
	StandardLogHandler.cpp \
	StandardLogHandlerFactory.cpp \
	StreamHandlerFactory.cpp \
	xlog.cpp

libfollylogging_la_LIBADD = $(top_builddir)/libfolly.la
libfollylogging_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION)
