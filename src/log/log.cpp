#include "log.h"

#include <chrono>

#include <cryengine/env.h>
#include <cryengine/IConsole.h>

#include <cwchar>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>
#include <windows.h>

#include "../util/StringUtils.h"

auto Filename = "kcd2db.log";
HANDLE ConsoleHandle = nullptr;

namespace
{
std::string GetFullPath(const wchar_t* path)
{
    const DWORD required = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (required == 0)
    {
        return "<unknown>";
    }

    std::wstring buffer(required, L'\0');
    const DWORD size = GetFullPathNameW(path, required, buffer.data(), nullptr);
    if (size == 0)
    {
        return "<unknown>";
    }

    buffer.resize(size);
    return kcd2db::WideToUtf8(buffer.c_str());
}
}

// LogLevel 枚举和配置结构
enum class LogLevel
{
    Debug, Info, Warn, Error, Off
};

LogLevel ConsoleLogLevel = LogLevel::Info;

struct LogConfig
{
    const char* colorCode;
    const char* plainPrefix;
    WORD consoleColor;
};

LogConfig GetLogConfig(const LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug:
        return {"$3", "[DEBUG] ", FOREGROUND_GREEN};
    case LogLevel::Info:
        return {"$5", "[INFO]  ", FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY};
    case LogLevel::Warn:
        return {"$6", "[WARN]  ", FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY};
    case LogLevel::Error:
        return {"$4", "[ERROR] ", FOREGROUND_RED | FOREGROUND_INTENSITY};
    default:
        return {"", "", FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN};
    }
}

bool ShouldWriteToConsole(const LogLevel level)
{
    return ConsoleLogLevel != LogLevel::Off && level >= ConsoleLogLevel;
}

bool TryParseLogLevel(const wchar_t* value, LogLevel& level)
{
    if (_wcsicmp(value, L"debug") == 0)
    {
        level = LogLevel::Debug;
        return true;
    }
    if (_wcsicmp(value, L"info") == 0)
    {
        level = LogLevel::Info;
        return true;
    }
    if (_wcsicmp(value, L"warn") == 0)
    {
        level = LogLevel::Warn;
        return true;
    }
    if (_wcsicmp(value, L"error") == 0)
    {
        level = LogLevel::Error;
        return true;
    }
    if (_wcsicmp(value, L"off") == 0)
    {
        level = LogLevel::Off;
        return true;
    }
    return false;
}

bool LoadConsoleLogLevelFromCommandLine()
{
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;

    bool hasInvalidValue = false;
    constexpr auto prefix = L"-kcd2dbConsoleLog=";
    constexpr auto prefixLength = sizeof(L"-kcd2dbConsoleLog=") / sizeof(wchar_t) - 1;

    for (int i = 1; i < argc; i++)
    {
        if (_wcsnicmp(argv[i], prefix, prefixLength) == 0)
        {
            LogLevel level = LogLevel::Info;
            if (TryParseLogLevel(argv[i] + prefixLength, level))
            {
                ConsoleLogLevel = level;
            }
            else
            {
                ConsoleLogLevel = LogLevel::Info;
                hasInvalidValue = true;
            }
        }
    }

    LocalFree(argv);
    return hasInvalidValue;
}

// 通用可变参数处理函数
static void LogVA(LogLevel level, const char* format, va_list args)
{
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

    const bool writeToConsole = ShouldWriteToConsole(level);

    // 写入系统控制台
    if (writeToConsole && ConsoleHandle)
    {
        CONSOLE_SCREEN_BUFFER_INFO originalInfo;
        GetConsoleScreenBufferInfo(ConsoleHandle, &originalInfo);

        SetConsoleTextAttribute(ConsoleHandle, config.consoleColor);
        std::string consoleMsg = std::string("[kcd2db]") + config.plainPrefix + message + "\n";
        DWORD written;
        WriteConsoleA(ConsoleHandle, consoleMsg.c_str(), consoleMsg.length(), &written, nullptr);
        SetConsoleTextAttribute(ConsoleHandle, originalInfo.wAttributes);
    }

    // 写入游戏控制台
    if (writeToConsole && gEnv && gEnv->pConsole)
    {
        std::string consoleMsg = std::string(config.colorCode) + "[kcd2db]" + config.plainPrefix + message + "\n";
        gEnv->pConsole->PrintLine(consoleMsg.c_str());
    }

    // 写入日志文件
    if (std::ofstream logFile(Filename, std::ios_base::app); logFile.is_open())
    {
        logFile << config.plainPrefix << message << std::endl;
    }
    else if (writeToConsole && ConsoleHandle)
    {
        const auto error = "[ERROR] Could not open log file.\n";
        DWORD written;
        WriteConsoleA(ConsoleHandle, error, strlen(error), &written, nullptr);
    }
}

// 系统控制台初始化
bool CheckForConsoleArg()
{
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;

    bool hasConsole = false;
    for (int i = 1; i < argc; i++)
    {
        if (_wcsicmp(argv[i], L"-console") == 0)
        {
            hasConsole = true;
            break;
        }
    }
    LocalFree(argv);
    return hasConsole;
}

void InitConsole()
{
    if (!CheckForConsoleArg()) return;
    AllocConsole();
    ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTitleA("Mod Debug Console");

    constexpr COORD bufferSize = {120, 9000};
    SetConsoleScreenBufferSize(ConsoleHandle, bufferSize);

    constexpr SMALL_RECT windowSize = {0, 0, 119, 30};
    SetConsoleWindowInfo(ConsoleHandle, TRUE, &windowSize);
}

// 日志系统初始化
void Log_init()
{
    const bool hasInvalidConsoleLogLevel = LoadConsoleLogLevelFromCommandLine();
    InitConsole();
    constexpr size_t max_size = 10 * 1024 * 1024;
    // 检查并清空过大的日志文件
    if (std::ifstream in(Filename, std::ios::ate | std::ios::binary);
        in.is_open() && in.tellg() > max_size)
    {
        in.close();
        std::ofstream(Filename, std::ios::trunc).close();
    }
    // 追加启动日志
    if (std::ofstream out(Filename, std::ios::app); out.is_open())
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &t);
        out << "\nLog initialized at " << std::put_time(&tm, "%F %T") << '\n';
        out << "[DEBUG] Log file path: " << GetFullPath(L"kcd2db.log") << '\n';
        if (hasInvalidConsoleLogLevel)
        {
            out << "[WARN]  Invalid -kcd2dbConsoleLog value; using info." << '\n';
        }
    }
    else
    {
        std::cerr << "Error opening log file: " << Filename << '\n';
    }
}


// 各级别日志函数
void LogDebug(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogVA(LogLevel::Debug, format, args);
    va_end(args);
}

void LogInfo(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogVA(LogLevel::Info, format, args);
    va_end(args);
}

void LogWarn(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogVA(LogLevel::Warn, format, args);
    va_end(args);
}

void LogError(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogVA(LogLevel::Error, format, args);
    va_end(args);
}

// 清理资源
void Log_close()
{
    if (ConsoleHandle)
    {
        FreeConsole();
        ConsoleHandle = nullptr;
    }
}
