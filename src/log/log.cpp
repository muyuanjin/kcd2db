#include "log.h"

#include <fstream>
#include <ostream>
#include <vector>
#include <windows.h>

auto Filename = "kcd2db.log";
HANDLE ConsoleHandle = nullptr;

bool CheckForConsoleArg() {
    LPWSTR cmdLine = GetCommandLineW();
    int argc;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);

    if (argv != nullptr) {
        for (int i = 1; i < argc; i++) {
            if (_wcsicmp(argv[i], L"-console") == 0) {
                LocalFree(argv);
                return true;
            }
        }
        LocalFree(argv);
    }
    return false;
}

void InitConsole() {
    if (!CheckForConsoleArg()) {
        return;
    }
    AllocConsole();
    ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTitleA("Mod Debug Console");

    COORD bufferSize = { 120, 9000 };
    SetConsoleScreenBufferSize(ConsoleHandle, bufferSize);

    SMALL_RECT windowSize = { 0, 0, 119, 30 };
    SetConsoleWindowInfo(ConsoleHandle, TRUE, &windowSize);
}


void Log_init()
{
    InitConsole();
    if (std::ofstream logFile(Filename); logFile.is_open())
    {
        logFile << "Log file initialized.\n";
        logFile.close();
    }
    else if (ConsoleHandle != nullptr)
    {
        const auto error = "[ERROR] Could not open log file.\n";
        DWORD written;
        WriteConsoleA(ConsoleHandle, error, strlen(error), &written, nullptr);
    }
}

void Log_impl(const std::string& message)
{
    // Write to console if enabled
    if (ConsoleHandle != nullptr)
    {
        const std::string consoleMsg = message + "\n";
        DWORD written;
        WriteConsoleA(ConsoleHandle, consoleMsg.c_str(), consoleMsg.length(), &written, nullptr);
    }

    // Always write to log file
    if (std::ofstream logFile(Filename, std::ios_base::app); logFile.is_open())
    {
        logFile << message << std::endl;
        logFile.close();
    }
    else if (ConsoleHandle != nullptr)
    {
        const auto error = "[ERROR] Could not open log file.\n";
        DWORD written;
        WriteConsoleA(ConsoleHandle, error, strlen(error), &written, nullptr);
    }
}

void Log(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    va_list args_copy;
    va_copy(args_copy, args);
    const int len = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);
    if (len < 0) {
        va_end(args);
        return;
    }
    const size_t buffer_size = static_cast<size_t>(len) + 1;
    std::vector<char> buffer(buffer_size);
    vsnprintf(buffer.data(), buffer_size, format, args);
    va_end(args);
    Log_impl(std::string(buffer.data(), len));
}

void Log_close()
{
    if (ConsoleHandle != nullptr)
    {
        FreeConsole();
    }
}