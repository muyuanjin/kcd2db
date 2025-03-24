#ifndef LOG_H
#define LOG_H
void Log_init();
void LogDebug(const char* format, ...);
void LogInfo(const char* format, ...);
void LogWarn(const char* format, ...);
void LogError(const char* format, ...);
void Log_close();
#endif //LOG_H
