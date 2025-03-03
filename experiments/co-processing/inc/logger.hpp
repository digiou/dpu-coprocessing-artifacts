#ifndef __MACROLOGGER_H__
#define __MACROLOGGER_H__

#ifdef __OBJC__
#import <Foundation/Foundation.h>
#else
#include <ctime>
#include <cstring>
#include <cstdio>
#include <sstream>

#endif

// === auxiliar functions
static inline char *timenow();

#define _FILE strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__

#define NO_LOG          0x00
#define ERROR_LEVEL     0x01
#define WARNING_LEVEL   0x02
#define INFO_LEVEL      0x03
#define DEBUG_LEVEL     0x04
#define TRACE_LEVEL     0x05

#ifndef LOG_LEVEL
#define LOG_LEVEL   INFO_LEVEL
#endif

// ANSI color codes
#define RESET_COLOR    "\033[0m"
#define RED_COLOR      "\033[31m"
#define GREEN_COLOR    "\033[32m"
#define YELLOW_COLOR   "\033[33m"
#define BLUE_COLOR     "\033[34m"
#define MAGENTA_COLOR  "\033[35m"
#define CYAN_COLOR     "\033[36m"
#define WHITE_COLOR    "\033[37m"

// Helper macros for colored log messages
#define COLOR_ERROR   RED_COLOR
#define COLOR_INFO    GREEN_COLOR
#define COLOR_DEBUG   CYAN_COLOR
#define COLOR_TRACE   MAGENTA_COLOR
#define COLOR_WARNING YELLOW_COLOR

#ifdef __OBJC__

#if __has_feature(objc_arc)
#define AUTORELEASEPOOL_BEGIN   @autoreleasepool {
#define AUTORELEASEPOOL_END     }
#define RELEASE(OBJ)            OBJ = nil
#else
#define AUTORELEASEPOOL_BEGIN   NSAutoreleasePool *_pool = [[NSAutoreleasePool alloc] init];
#define AUTORELEASEPOOL_END     [_pool release];
#define RELEASE(OBJ)            [OBJ release];
#endif

#define PRINTFUNCTION(format, ...)      objc_print(@format, __VA_ARGS__)
#else
#define PRINTFUNCTION(format, ...)      fprintf(stderr, format, __VA_ARGS__)

#endif

#define LOG_FMT             "%s | %-7s | %-15s | %s:%d | "
#define LOG_ARGS(LOG_TAG)   timenow(), LOG_TAG, _FILE, __FUNCTION__, __LINE__

#define NEWLINE     "\n"

#define ERROR_TAG   "ERROR"
#define WARNING_TAG "WARNING"
#define INFO_TAG    "INFO"
#define DEBUG_TAG   "DEBUG"
#define TRACE_TAG   "TRACE"

#if LOG_LEVEL >= TRACE_LEVEL
#define LOG_TRACE(message, args...)     PRINTFUNCTION(COLOR_TRACE LOG_FMT message RESET_COLOR NEWLINE, LOG_ARGS(TRACE_TAG), ## args)
#else
#define LOG_TRACE(message, args...)
#endif

#if LOG_LEVEL >= DEBUG_LEVEL
#define LOG_DEBUG(message, args...)     PRINTFUNCTION(COLOR_DEBUG LOG_FMT message RESET_COLOR NEWLINE, LOG_ARGS(DEBUG_TAG), ## args)
#else
#define LOG_DEBUG(message, args...)
#endif

#if LOG_LEVEL >= INFO_LEVEL
#define LOG_INFO(message, args...)      PRINTFUNCTION(COLOR_INFO LOG_FMT message RESET_COLOR NEWLINE, LOG_ARGS(INFO_TAG), ## args)
#else
#define LOG_INFO(message, args...)
#endif

#if LOG_LEVEL >= WARNING_LEVEL
#define LOG_WARNING(message, args...)      PRINTFUNCTION(COLOR_WARNING LOG_FMT message RESET_COLOR NEWLINE, LOG_ARGS(WARNING_TAG), ## args)
#else
#define LOG_WARNING(message, args...)
#endif

#if LOG_LEVEL >= ERROR_LEVEL
#define LOG_ERROR(message, args...)     PRINTFUNCTION(COLOR_ERROR LOG_FMT message RESET_COLOR NEWLINE, LOG_ARGS(ERROR_TAG), ## args)
#else
#define LOG_ERROR(message, args...)
#endif

#if LOG_LEVEL >= ERROR_LEVEL
#define LOG_ERROR_AND_THROW(message, args...)  do { \
        PRINTFUNCTION(COLOR_ERROR LOG_FMT message RESET_COLOR NEWLINE, LOG_ARGS(ERROR_TAG), ## args); \
        throw std::runtime_error(message); \
    } while (0)
#else
#define LOG_ERROR_AND_THROW(message, args...)
#endif

#if LOG_LEVEL >= NO_LOGS
#define LOG_IF_ERROR(condition, message, args...) if (condition) PRINTFUNCTION(COLOR_ERROR LOG_FMT message RESET_COLOR NEWLINE, LOG_ARGS(ERROR_TAG), ## args)
#else
#define LOG_IF_ERROR(condition, message, args...)
#endif

static inline char *timenow() {
    static char buffer[64];
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer, 64, "%Y-%m-%d %H:%M:%S", timeinfo);

    return buffer;
}

#ifdef __OBJC__

static inline void objc_print(NSString *format, ...) {
    AUTORELEASEPOOL_BEGIN
    va_list args;
    va_start(args, format);
    NSString *logStr = [[NSString alloc] initWithFormat:format arguments:args];
    fprintf(stderr, "%s", [logStr UTF8String]);
    RELEASE(logStr);
    va_end(args);
    AUTORELEASEPOOL_END
}

#endif

#endif
