#include "lume/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static lume_log_level current_level = LUME_LOG_INFO;

static const char *level_name(lume_log_level level)
{
    switch (level) {
    case LUME_LOG_DEBUG:
        return "DEBUG";
    case LUME_LOG_INFO:
        return "INFO";
    case LUME_LOG_WARN:
        return "WARN";
    case LUME_LOG_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

void lume_log_set_level(lume_log_level level)
{
    current_level = level;
}

void lume_log_message(lume_log_level level, const char *format, ...)
{
    time_t now;
    struct tm local;
    char timestamp[32];
    va_list args;

    if (level < current_level) {
        return;
    }

    now = time(NULL);
    localtime_r(&now, &local);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);

    fprintf(stderr, "[%s] %-5s ", timestamp, level_name(level));

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fputc('\n', stderr);
}
