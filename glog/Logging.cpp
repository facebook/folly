#include "Logging.h"

#define _GNU_SOURCE 1 // needed for O_NOFOLLOW and pread()/pwrite()

#include <thread>
#include <mutex>
#include <algorithm>
#include <assert.h>
#include <iomanip>
#include <string>
#include <climits>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdio>
#include <iostream>
#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <errno.h>
#include <sstream>
#include <sys/time.h>

#include "Logging.h"


using std::string;
using std::vector;
using std::setw;
using std::setfill;
using std::hex;
using std::dec;
using std::min;
using std::ostream;
using std::ostringstream;
using std::FILE;
using std::fwrite;
using std::fclose;
using std::fflush;
using std::fprintf;
using std::perror;

namespace libslack {

const double DELTA_DOUBLE = 0.000001;
const double DELTA_FLOAT  = 0.000001;

const std::string SEP = "=======================================================";
static enum LogSeverity FLAGS_minlogSeverity = LogSeverity::INFO;
static const size_t kMaxLogMessageLen = 30000;
static std::mutex g_cout_mutex;

struct LoggingInitializer {
    LoggingInitializer() {
        LoggingInit();
    }

    ~LoggingInitializer() {
        LoggingShutdown();
    }
};

static LoggingInitializer initializer;

struct LogMessage::LogMessageData  {
    LogMessageData();
    ~LogMessageData();
    int preserved_errno_;
    char message_text_[kMaxLogMessageLen+1];
    LogStream stream_;
    char severity_;
    int line_;
    void (LogMessage::*send_method_)();
    std::string* message_;             // NULL or string to write message into
    time_t timestamp_;            // Time of creation of LogMessage
    struct ::tm tm_time_;         // Time of creation of LogMessage
    size_t num_prefix_chars_;     // # of chars of prefix in this message
    size_t num_chars_to_log_;     // # of chars of msg to send to log
    size_t num_chars_to_syslog_;  // # of chars of msg to send to syslog
    const char* basename_;        // basename of file that called LOG
    const char* fullname_;        // fullname of file that called LOG
    bool has_been_flushed_;       // false => data has not been flushed
    bool first_fatal_;            // true => this was first fatal msg

private:
    LogMessageData(const LogMessageData&);
    void operator=(const LogMessageData&);
};

static const char* GetLogSeverityName(LogSeverity severity) {
    switch  (severity) {
        case LogSeverity::TESTS:        return "T";
        case LogSeverity::NEVER:        return "N";
        case LogSeverity::LOG_DEBUG:        return "D";
        case LogSeverity::INFO:         return "I";
        case LogSeverity::WARNING:      return "W";
        case LogSeverity::ERROR:        return "E";
        case LogSeverity::FATAL:        return "F";
    }
    return "U";
}

const char* const_basename(const char* filepath) {
    const char* base = strrchr(filepath, '/');
    return base ? (base+1) : filepath;
}

LogMessage::LogMessageData::LogMessageData()
: stream_(message_text_, kMaxLogMessageLen) {
}

LogMessage::LogMessageData::~LogMessageData() {
}

LogMessage::LogMessage(const char* file, int line)
: data_(NULL) {
    Init(file, line, LogSeverity::INFO, &LogMessage::SendToLog);
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity)
: data_(NULL) {
    Init(file, line, severity, &LogMessage::SendToLog);
}

void LogMessage::Init(const char* file,
                      int line,
                      LogSeverity severity,
                      void (LogMessage::*send_method)())
{
    data_ = new LogMessageData();
    data_->first_fatal_ = false;

    stream().fill('0');
    data_->preserved_errno_ = errno;
    data_->severity_ = severity;
    data_->line_ = line;
    data_->send_method_ = send_method;
    data_->basename_ = const_basename(file);
    time( &data_->timestamp_ );
    localtime_r(&data_->timestamp_, &data_->tm_time_);

    struct timeval tv;
    gettimeofday(&tv, NULL);

    data_->num_chars_to_log_ = 0;
    data_->num_chars_to_syslog_ = 0;
    data_->fullname_ = file;
    data_->has_been_flushed_ = false;

    const auto thread_id = std::this_thread::get_id();

    this->stream()
        << "[["
        << GetLogSeverityName(severity)
        << setw(2) << 1+data_->tm_time_.tm_mon
        << setw(2) << data_->tm_time_.tm_mday
        << ' '
        << setw(2) << data_->tm_time_.tm_mday  << '/'
        << setw(2) << data_->tm_time_.tm_mon
        << ' '
        << setw(2) << data_->tm_time_.tm_hour  << ':'
        << setw(2) << data_->tm_time_.tm_min   << ':'
        << setw(2) << data_->tm_time_.tm_sec
        << ' '
        << tv.tv_sec << '.'
        << tv.tv_usec
        << ' '
        << setfill(' ') << setw(5)
        << thread_id << setfill('0')
        << ' '
        << data_->basename_ << ':' << data_->line_
        << "]] ";

    data_->num_prefix_chars_ = data_->stream_.pcount();
}

LogMessage::~LogMessage() {
    Flush();
    delete data_;
}

int LogMessage::preserved_errno() const {
    return data_->preserved_errno_;
}

ostream& LogMessage::stream() {
    return data_->stream_;
}

// Flush buffered message, called by the destructor, or any other function
// that needs to synchronize the log.
void LogMessage::Flush() {
    if (data_->has_been_flushed_ || data_->severity_ < FLAGS_minlogSeverity) {
        return;
    }

    data_->num_chars_to_log_ = data_->stream_.pcount();
    data_->num_chars_to_syslog_ =
    data_->num_chars_to_log_ - data_->num_prefix_chars_;

    // Do we need to add a \n to the end of this message?
    bool append_newline =
        (data_->message_text_[data_->num_chars_to_log_-1] != '\n');
    char original_final_char = '\0';

    // If we do need to add a \n, we'll do it by violating the memory of the
    // ostrstream buffer.  This is quick, and we'll make sure to undo our
    // modification before anything else is done with the ostrstream.  It
    // would be preferable not to do things this way, but it seems to be
    // the best way to deal with this.
    if (append_newline) {
        original_final_char = data_->message_text_[data_->num_chars_to_log_];
        data_->message_text_[data_->num_chars_to_log_++] = '\n';
    }

    (this->*(data_->send_method_))();

    if (append_newline) {
        // Fix the ostrstream back how it was before we screwed with it.
        // It's 99.44% certain that we don't need to worry about doing this.
        data_->message_text_[data_->num_chars_to_log_-1] = original_final_char;
    }

    // If errno was already set before we enter the logging call, we'll
    // set it back to that value when we return from the logging call.
    // It happens often that we log an error message after a syscall
    // failure, which can potentially set the errno to some other
    // values.  We would like to preserve the original errno.
    if (data_->preserved_errno_ != 0) {
        errno = data_->preserved_errno_;
    }

    // Note that this message is now safely logged.  If we're asked to flush
    // again, as a result of destruction, say, we'll do nothing on future calls.
    data_->has_been_flushed_ = true;

    if (data_->severity_ == LogSeverity::FATAL) {
        abort();
    }
}

void LogMessage::SendToLog() {
    // do not log empty lines:
    if ((data_->num_chars_to_log_ - data_->num_prefix_chars_) > 1) {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout.write(data_->message_text_, data_->num_chars_to_log_);
    }
}

void LoggingInit() {
    LOG(INFO) << "logging initialized";
}

void LoggingSetMinSeverity(LogSeverity minlogSeverity) {
    FLAGS_minlogSeverity = minlogSeverity;
}

void LoggingShutdown() {
    LOG(INFO) << "logging shutdown";
}

void LoggingTest() {
    LOG(TESTS) << SEP;

    std::string message = "i am the batman";
    for (unsigned int i = 0; i < 1; ++i) {
        LOG(NEVER)
            << "hello, one, " << 1
            << ", two, " << 2
            << ", three, " << 3
            << ", " << message;

        LOG(INFO)
            << "hello, one, " << 1
            << ", two, " << 2
            << ", three, " << 3
            << ", i, " << i
            << ", " << message;

        LOG(WARNING)
            << "hello, one, " << 1
            << ", two, " << 2
            << ", three, " << 3
            << ", i, " << i
            << ", " << message;

        LOG(ERROR)
            << "hello, one, " << 1
            << ", two, " << 2
            << ", three, " << 3
            << ", i, " << i
            << ", " << message;
    }

    LOG(TESTS) << "message, " << message;

    CHECK_EQ(10, 10)                << "batman is happy! -> NO PRINT";
    CHECK_EQ("a", "a")              << "batman is happy! -> NO PRINT";
    CHECK_EQ(1.0000001, 1.0000001)  << "batman is happy! -> NO PRINT";
    CHECK_EQ(1.0000001, 1.0000002)  << "batman is happy! -> NO PRINT";
    CHECK_EQ(1.0, 1.0)              << "batman is happy! -> NO PRINT";
    CHECK_NE("one", "two")          << "batman is happy! -> NO PRINT";
    CHECK_NE(1, 2)                  << "batman is happy! -> NO PRINT";

    CHECK_LE_SEV(1, 2, libslack::LogSeverity::TESTS)         << "batman is happy! -> NO PRINT";
    CHECK_LE_SEV("A", "B", libslack::LogSeverity::TESTS)     << "batman is happy! -> NO PRINT";
    CHECK_LE_SEV(2, 1, libslack::LogSeverity::TESTS)         << "batman is sad, will print this error!";

    CHECK_EQ_SEV(1.1, 1.2, libslack::LogSeverity::TESTS)     << "batman is sad, will print this error!";
    CHECK_EQ_SEV("one", "two", libslack::LogSeverity::TESTS) << "batman is sad, will print this error!";

    CHECK_GT_SEV(1.5, 1.1, libslack::LogSeverity::TESTS)     << "batman is happy! -> NO PRINT";
    CHECK_GT_SEV(1.1, 1.5, libslack::LogSeverity::TESTS)     << "batman is sad, will print this error!";

    CHECK_GE_SEV(1.1, 1.1, libslack::LogSeverity::TESTS)     << "batman is happy! -> NO PRINT";
    CHECK_GE_SEV(1.1, 1.2, libslack::LogSeverity::TESTS)     << "batman is sad, will print this error!";

    CHECK_LT_SEV(1.1, 1.2, libslack::LogSeverity::TESTS)     << "batman is happy! -> NO PRINT";
    CHECK_LT_SEV(1.2, 1.1, libslack::LogSeverity::TESTS)     << "batman is sad, will print this error!";

    // This should kill the process:
    // LOG(FATAL) << "die!";

    LOG(TESTS) << SEP;
}

}