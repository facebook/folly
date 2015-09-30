#pragma once

#include <errno.h>
#include <string.h>
#include <time.h>
#include <iosfwd>
#include <ostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <cmath>
#include <stdint.h>
#include <sys/types.h>
#include <inttypes.h>

/////////////////////////////////////////////////////////////////
///
/// Mostly borrowed from : https://github.com/google/glog
///
/////////////////////////////////////////////////////////////////

#define LOG(severity)  \
    libslack::LogMessage(__FILE__, __LINE__, libslack::LogSeverity::severity).stream()
#define VLOG(severity) \
    libslack::LogMessage(__FILE__, __LINE__, libslack::LogSeverity::NEVER).stream()


namespace libslack
{
    extern const std::string SEP;
    enum LogSeverity
    {
        NEVER = -1,
        LS_DEBUG = 0,
        TESTS,
        INFO,
        WARNING,
        ERROR,
        FATAL
    };

    void LoggingInit();
    void LoggingShutdown();
    void LoggingSetMinSeverity(libslack::LogSeverity minlogSeverity);
    void LoggingTest();

    namespace base_logging
    {
        // LogMessage::LogStream is a std::ostream backed by this streambuf.
        // This class ignores overflow and leaves two bytes at the end of the
        // buffer to allow for a '\n' and '\0'.
        class LogStreamBuf : public std::streambuf
        {
        public:
            // REQUIREMENTS: "len" must be >= 2 to account for the '\n' and '\n'.
            LogStreamBuf(char* buf, int len)
            {
                setp(buf, buf + len - 2);
            }

            virtual int_type overflow(int_type ch)
            {
                return ch;
            }

            size_t pcount() const
            {
                return pptr() - pbase();
            }

            char* pbase() const
            {
                return std::streambuf::pbase();
            }
        };
    }

    class LogMessage
    {
    public:
        enum
        {
            // Passing kNoLogPrefix for the line number disables the
            // log-message prefix. Useful for using the LogMessage
            // infrastructure as a printing utility. See also the --log_prefix
            // flag for controlling the log-message prefix on an
            // application-wide basis.
                kNoLogPrefix = -1
        };

        class LogStream : public std::ostream
        {
        public:
            LogStream(char* buf, int len) :
                std::ostream(NULL),
                streambuf_(buf, len),
                self_(this)
            {
                rdbuf(&streambuf_);
            }

            LogStream* self() const
            {
                return self_;
            }

            // Legacy std::streambuf methods.
            size_t pcount() const
            {
                return streambuf_.pcount();
            }

            char* pbase() const
            {
                return streambuf_.pbase();
            }

            char* str() const
            {
                return pbase();
            }

        private:
            LogStream(const LogStream&);
            LogStream& operator=(const LogStream&);
            base_logging::LogStreamBuf streambuf_;
            LogStream* self_;  // Consistency check hack
        };

    public:
        // icc 8 requires this typedef to avoid an internal compiler error.
        typedef void (LogMessage::*SendMethod)();

        LogMessage(const char* file,
                   int line);
        LogMessage(const char* file,
                   int line,
                   libslack::LogSeverity severity);
        ~LogMessage();

        void Flush();
        void SendToLog();
        std::ostream& stream();
        int preserved_errno() const;
        struct LogMessageData;

    protected:
        void Init(const char* file,
                  int line,
                  libslack::LogSeverity severity,
                  void (LogMessage::*send_method)());

        LogMessageData* data_;

        LogMessage(const LogMessage&);
        void operator=(const LogMessage&);
    };
}

namespace libslack
{

    extern const double DELTA_DOUBLE;
    extern const double DELTA_FLOAT;

    inline bool check_eq(int val1, int val2)
    {
        return (val1 == val2);
    }

    inline bool check_eq(double val1, double val2)
    {
        return std::abs(val1 - val2) < DELTA_DOUBLE;
    }

    inline bool check_eq(float val1, float val2)
    {
        return std::abs(val1 - val2) < DELTA_FLOAT;
    }

    template<typename T>
    bool check_eq(const T& val1, const T& val2)
    {
        return val1 == val2;
    }
}

#define CHECK_SEV(_cond, _severity)                                             \
    for (unsigned int __CHECK_EQ_SEV_C__= 0;                                    \
            __CHECK_EQ_SEV_C__ < 1 && (!(_cond));                               \
            ++__CHECK_EQ_SEV_C__)                                               \
        libslack::LogMessage(__FILE__, __LINE__, _severity).stream()            \
            << "check " << "check " << ", failed, cond, " << _cond              \
            << " ; "

#define CHECK_EQ_SEV(_val1, _val2, _severity)                                   \
    for (unsigned int __CHECK_EQ_SEV_C__= 0;                                    \
            __CHECK_EQ_SEV_C__ < 1 && !libslack::check_eq(_val1, _val2);        \
            ++__CHECK_EQ_SEV_C__)                                               \
        libslack::LogMessage(__FILE__, __LINE__, _severity).stream()            \
            << "check " << "check_eq" << ", failed, val1, " << _val1            \
            << ", val2, " << _val2 << " ; "

#define CHECK_NE_SEV(_val1, _val2, _severity)                                   \
    for (unsigned int __CHECK_EQ_SEV_C__= 0;                                    \
            __CHECK_EQ_SEV_C__ < 1 && libslack::check_eq(_val1, _val2);         \
            ++__CHECK_EQ_SEV_C__)                                               \
        libslack::LogMessage(__FILE__, __LINE__, _severity).stream()            \
            << "check " << "check_ne" << ", failed, val1, " << _val1            \
            << ", val2, " << _val2 << " ; "

#define CHECK_LE_SEV(_val1, _val2, _severity)                                   \
    for (unsigned int __CHECK_EQ_SEV_C__= 0;                                    \
            __CHECK_EQ_SEV_C__ < 1 && !((_val1)<=(_val2));                      \
            ++__CHECK_EQ_SEV_C__)                                               \
        libslack::LogMessage(__FILE__, __LINE__, _severity).stream()            \
            << "check " << "check_le" << ", failed, val1, " << _val1            \
            << ", val2, " << _val2 << " ; "

#define CHECK_LT_SEV(_val1, _val2, _severity)                                   \
    for (unsigned int __CHECK_EQ_SEV_C__= 0;                                    \
            __CHECK_EQ_SEV_C__ < 1 && !((_val1)<(_val2));                       \
            ++__CHECK_EQ_SEV_C__)                                               \
        libslack::LogMessage(__FILE__, __LINE__, _severity).stream()            \
            << "check " << "check_lt" << ", failed, val1, " << _val1            \
            << ", val2, " << _val2 << " ; "

#define CHECK_GE_SEV(_val1, _val2, _severity)                                   \
    for (unsigned int __CHECK_EQ_SEV_C__= 0;                                    \
            __CHECK_EQ_SEV_C__ < 1 && !((_val1)>=(_val2));                      \
            ++__CHECK_EQ_SEV_C__)                                               \
        libslack::LogMessage(__FILE__, __LINE__, _severity).stream()            \
            << "check " << "check_ge" << ", failed, val1, " << _val1            \
            << ", val2, " << _val2 << " ; "

#define CHECK_GT_SEV(_val1, _val2, _severity)                                   \
    for (unsigned int __CHECK_EQ_SEV_C__= 0;                                    \
            __CHECK_EQ_SEV_C__ < 1 && !((_val1)>(_val2));                       \
            ++__CHECK_EQ_SEV_C__)                                               \
        libslack::LogMessage(__FILE__, __LINE__, _severity).stream()            \
            << "check " << "check_gt" << ", failed, val1, " << _val1            \
            << ", val2, " << _val2 << " ; "


#define CHECK_EQ(_val1, _val2)  CHECK_EQ_SEV(_val1, _val2, libslack::LogSeverity::FATAL)
#define CHECK_NE(_val1, _val2)  CHECK_NE_SEV(_val1, _val2, libslack::LogSeverity::FATAL)
#define CHECK_LT(_val1, _val2)  CHECK_LT_SEV(_val1, _val2, libslack::LogSeverity::FATAL)
#define CHECK_GE(_val1, _val2)  CHECK_GE_SEV(_val1, _val2, libslack::LogSeverity::FATAL)
#define CHECK_GT(_val1, _val2)  CHECK_GT_SEV(_val1, _val2, libslack::LogSeverity::FATAL)
#define CHECK(_cond)            CHECK_SEV((_cond), libslack::LogSeverity::FATAL)

#define DCHECK_NE               CHECK_NE
#define DCHECK_GT               CHECK_GT
#define DCHECK                  CHECK
#define DCHECK_EQ               CHECK_EQ

