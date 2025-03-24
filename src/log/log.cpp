#include "log.h"

#include "../kcd2/env.h"
#include "../kcd2/IConsole.h"

#include <fstream>
#include <ostream>
#include <vector>
#include <windows.h>

auto Filename = "kcd2db.log";
HANDLE ConsoleHandle = nullptr;

// LogLevel 枚举和配置结构
enum class LogLevel {
    Debug, Info, Warn, Error
};

struct LogConfig {
    const char* colorPrefix;
    const char* plainPrefix;
    WORD consoleColor;
};

LogConfig GetLogConfig(const LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return { "$3[DEBUG] ", "[DEBUG] ", FOREGROUND_GREEN };
        case LogLevel::Info:
            return { "$5[INFO]  ", "[INFO]  ", FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY };
        case LogLevel::Warn:
            return { "$6[WARN]  ", "[WARN]  ", FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY };
        case LogLevel::Error:
            return { "$4[ERROR] ", "[ERROR] ", FOREGROUND_RED | FOREGROUND_INTENSITY };
        default:
            return { "", "", FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN };
    }
}

// 通用可变参数处理函数
static void LogVA(LogLevel level, const char* format, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    if (len < 0) return;

    std::vector<char> buf(len + 1);
    vsnprintf(buf.data(), len + 1, format, args);
    std::string message(buf.data(), len);

    // 获取配置
    const auto config = GetLogConfig(level);

    // 写入系统控制台
    if (ConsoleHandle) {
        CONSOLE_SCREEN_BUFFER_INFO originalInfo;
        GetConsoleScreenBufferInfo(ConsoleHandle, &originalInfo);

        SetConsoleTextAttribute(ConsoleHandle, config.consoleColor);
        std::string consoleMsg = config.plainPrefix + message + "\n";
        DWORD written;
        WriteConsoleA(ConsoleHandle, consoleMsg.c_str(), consoleMsg.length(), &written, nullptr);
        SetConsoleTextAttribute(ConsoleHandle, originalInfo.wAttributes);
    }

    // 写入游戏控制台
    if (gEnv && gEnv->pConsole) {
        std::string consoleMsg = config.colorPrefix + message + "\n";
        gEnv->pConsole->PrintLine(consoleMsg.c_str());
    }

    // 写入日志文件
    if (std::ofstream logFile(Filename, std::ios_base::app); logFile.is_open()) {
        logFile << config.plainPrefix << message << std::endl;
    } else if (ConsoleHandle) {
        const auto error = "[ERROR] Could not open log file.\n";
        DWORD written;
        WriteConsoleA(ConsoleHandle, error, strlen(error), &written, nullptr);
    }
}

// 系统控制台初始化
bool CheckForConsoleArg() {
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;

    bool hasConsole = false;
    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"-console") == 0) {
            hasConsole = true;
            break;
        }
    }
    LocalFree(argv);
    return hasConsole;
}

void InitConsole() {
    if (!CheckForConsoleArg()) return;

    AllocConsole();
    ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTitleA("Mod Debug Console");

    const COORD bufferSize = { 120, 9000 };
    SetConsoleScreenBufferSize(ConsoleHandle, bufferSize);

    const SMALL_RECT windowSize = { 0, 0, 119, 30 };
    SetConsoleWindowInfo(ConsoleHandle, TRUE, &windowSize);
}

// 日志系统初始化
void Log_init() {
    InitConsole();
    if (std::ofstream logFile(Filename); logFile) {
        logFile << "Log file initialized.\n";
    }
}

// 各级别日志函数
void LogDebug(const char* format, ...) { va_list args; va_start(args, format); LogVA(LogLevel::Debug, format, args); va_end(args); }
void LogInfo(const char* format, ...)  { va_list args; va_start(args, format); LogVA(LogLevel::Info, format, args);  va_end(args); }
void LogWarn(const char* format, ...)  { va_list args; va_start(args, format); LogVA(LogLevel::Warn, format, args);  va_end(args); }
void LogError(const char* format, ...) { va_list args; va_start(args, format); LogVA(LogLevel::Error, format, args); va_end(args); }

// 清理资源
void Log_close() {
    if (ConsoleHandle) {
        FreeConsole();
        ConsoleHandle = nullptr;
    }
}
