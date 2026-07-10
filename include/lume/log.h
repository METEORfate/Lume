#ifndef LUME_LOG_H
#define LUME_LOG_H

typedef enum lume_log_level {
    LUME_LOG_DEBUG = 0,
    LUME_LOG_INFO = 1,
    LUME_LOG_WARN = 2,
    LUME_LOG_ERROR = 3
} lume_log_level;

void lume_log_set_level(lume_log_level level);
void lume_log_message(lume_log_level level, const char *format, ...);

#define LUME_LOGD(...) lume_log_message(LUME_LOG_DEBUG, __VA_ARGS__)
#define LUME_LOGI(...) lume_log_message(LUME_LOG_INFO, __VA_ARGS__)
#define LUME_LOGW(...) lume_log_message(LUME_LOG_WARN, __VA_ARGS__)
#define LUME_LOGE(...) lume_log_message(LUME_LOG_ERROR, __VA_ARGS__)

#endif
