#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include "LuaRunner.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <cryengine/IScriptSystem.h>

#include "../log/log.h"

namespace
{
constexpr std::uint16_t kDefaultPort = 28771;
constexpr std::uint32_t kMaxMessageLength = 1024 * 1024;
constexpr int kClientSocketTimeoutMs = 60 * 1000;
constexpr auto kExecutionTimeout = std::chrono::seconds(60);
constexpr auto kInvalidSocket = static_cast<std::uintptr_t>(INVALID_SOCKET);
constexpr std::string_view kNativeProtocolHeader = "KCD2DB_LUA_RUNNER/1";

struct RunnerOptions
{
    bool enabled = false;
    std::uint16_t port = kDefaultPort;
};

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
    if (required <= 1)
    {
        return {};
    }

    std::wstring result(required, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, result.data(), required) == 0)
    {
        return {};
    }
    result.resize(required - 1);
    return result;
}

std::string BaseName(const std::string& path)
{
    const size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool ReadFileUtf8Path(const std::string& path, std::string& content)
{
    const std::wstring widePath = Utf8ToWide(path);
    if (widePath.empty())
    {
        return false;
    }

    std::ifstream file(std::filesystem::path(widePath), std::ios::binary);
    if (!file)
    {
        return false;
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    content = stream.str();
    return true;
}

bool ExecuteScriptFile(IScriptSystem* scriptSystem, const std::string& path, const LuaRunner::ExecutionMode mode)
{
    if (mode == LuaRunner::ExecutionMode::File)
    {
        return scriptSystem->ExecuteFile(path.c_str(), true, true);
    }

    std::string content;
    if (ReadFileUtf8Path(path, content))
    {
        return scriptSystem->ExecuteBuffer(content.data(), content.size(), path.c_str());
    }

    if (mode == LuaRunner::ExecutionMode::Buffer)
    {
        return false;
    }

    return scriptSystem->ExecuteFile(path.c_str(), true, true);
}

bool SendAll(SOCKET socket, const std::string& message)
{
    const char* data = message.data();
    int remaining = static_cast<int>(message.size());
    while (remaining > 0)
    {
        const int sent = send(socket, data, remaining, 0);
        if (sent == SOCKET_ERROR)
        {
            return false;
        }
        data += sent;
        remaining -= sent;
    }
    return true;
}

bool RecvAll(SOCKET socket, char* buffer, int length)
{
    int total = 0;
    while (total < length)
    {
        const int received = recv(socket, buffer + total, length - total, 0);
        if (received <= 0)
        {
            return false;
        }
        total += received;
    }
    return true;
}

bool SetClientSocketTimeouts(const SOCKET socket)
{
    constexpr int timeoutMs = kClientSocketTimeoutMs;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs))
        == SOCKET_ERROR)
    {
        return false;
    }
    if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs))
        == SOCKET_ERROR)
    {
        return false;
    }
    return true;
}

void ShutdownAndCloseSocket(const SOCKET socket)
{
    shutdown(socket, SD_BOTH);
    closesocket(socket);
}

std::vector<std::string> SplitPaths(std::string payload)
{
    if (const size_t nul = payload.find('\0'); nul != std::string::npos)
    {
        payload.resize(nul);
    }

    std::vector<std::string> paths;
    std::stringstream stream(payload);
    std::string path;
    while (std::getline(stream, path, ','))
    {
        if (!path.empty())
        {
            paths.push_back(path);
        }
    }
    return paths;
}

std::string TrimAscii(std::string_view value)
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
    {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
    {
        value.remove_suffix(1);
    }
    return std::string(value);
}

bool TryParseMode(std::string_view value, LuaRunner::ExecutionMode& mode)
{
    if (value == "auto")
    {
        mode = LuaRunner::ExecutionMode::Auto;
        return true;
    }
    if (value == "buffer")
    {
        mode = LuaRunner::ExecutionMode::Buffer;
        return true;
    }
    if (value == "file")
    {
        mode = LuaRunner::ExecutionMode::File;
        return true;
    }
    return false;
}

std::string NativeResponse(const bool ok, std::string_view message)
{
    std::string response(kNativeProtocolHeader);
    response += "\nok=";
    response += ok ? "true" : "false";
    response += "\nmessage=";
    response += message;
    return response;
}

bool ParseNativePayload(const std::string& payload, LuaRunner::Request& request, std::string& immediateResponse)
{
    std::stringstream stream(payload);
    std::string line;
    if (!std::getline(stream, line) || TrimAscii(line) != kNativeProtocolHeader)
    {
        return false;
    }

    request.protocol = LuaRunner::Protocol::Kcd2Db;
    std::string command = "run";
    while (std::getline(stream, line))
    {
        if (line.empty() || line == "\r")
        {
            continue;
        }

        const size_t equals = line.find('=');
        if (equals == std::string::npos)
        {
            immediateResponse = NativeResponse(false, "invalid request line");
            return true;
        }

        const std::string key = TrimAscii(std::string_view(line).substr(0, equals));
        const std::string value = TrimAscii(std::string_view(line).substr(equals + 1));
        if (key == "command")
        {
            command = value;
        }
        else if (key == "mode")
        {
            if (!TryParseMode(value, request.mode))
            {
                immediateResponse = NativeResponse(false, "invalid execution mode");
                return true;
            }
        }
        else if (key == "path")
        {
            if (!value.empty())
            {
                request.paths.push_back(value);
            }
        }
    }

    if (command == "ping")
    {
        immediateResponse = NativeResponse(true, "pong");
        return true;
    }

    if (command != "run")
    {
        immediateResponse = NativeResponse(false, "unknown command");
        return true;
    }

    if (request.paths.empty())
    {
        immediateResponse = NativeResponse(false, "no script paths received");
        return true;
    }

    return true;
}

void ParsePayload(std::string payload, LuaRunner::Request& request, std::string& immediateResponse)
{
    if (const size_t nul = payload.find('\0'); nul != std::string::npos)
    {
        payload.resize(nul);
    }

    if (payload.starts_with(kNativeProtocolHeader))
    {
        ParseNativePayload(payload, request, immediateResponse);
        return;
    }

    request.paths = SplitPaths(std::move(payload));
}

bool TryParsePort(const wchar_t* value, std::uint16_t& port)
{
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(value, &end, 10);
    if (!end || *end != L'\0' || parsed == 0 || parsed > 65535)
    {
        return false;
    }
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

RunnerOptions ParseCommandLine()
{
    RunnerOptions options;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        return options;
    }

    constexpr auto runnerArg = L"-kcd2dbLuaRunner";
    constexpr auto runnerArgLength = sizeof(L"-kcd2dbLuaRunner") / sizeof(wchar_t) - 1;
    bool invalidPort = false;

    for (int i = 1; i < argc; ++i)
    {
        if (_wcsicmp(argv[i], runnerArg) == 0)
        {
            options.enabled = true;
            continue;
        }

        if (_wcsnicmp(argv[i], runnerArg, runnerArgLength) == 0 && argv[i][runnerArgLength] == L'=')
        {
            options.enabled = true;
            if (!TryParsePort(argv[i] + runnerArgLength + 1, options.port))
            {
                options.port = kDefaultPort;
                invalidPort = true;
            }
        }
    }

    LocalFree(argv);

    if (invalidPort)
    {
        LogWarn("Invalid -kcd2dbLuaRunner port value; using %u.", kDefaultPort);
    }
    return options;
}
}

LuaRunner& LuaRunner::Instance()
{
    static LuaRunner runner;
    return runner;
}

bool LuaRunner::StartFromCommandLine()
{
    const RunnerOptions options = ParseCommandLine();
    if (!options.enabled)
    {
        return false;
    }

    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
    {
        return true;
    }

    std::thread(&LuaRunner::ServerThread, this, options.port).detach();
    LogInfo("Lua runner enabled on 127.0.0.1:%u.", options.port);
    return true;
}

void LuaRunner::Stop()
{
    if (!m_running.exchange(false))
    {
        return;
    }

    const auto socketValue = m_listenSocket.exchange(kInvalidSocket);
    if (socketValue != kInvalidSocket)
    {
        ShutdownAndCloseSocket(static_cast<SOCKET>(socketValue));
    }

    std::vector<std::uintptr_t> clientSockets;
    {
        std::lock_guard lock(m_socketMutex);
        clientSockets.assign(m_clientSockets.begin(), m_clientSockets.end());
        m_clientSockets.clear();
    }

    for (const auto clientSocket : clientSockets)
    {
        ShutdownAndCloseSocket(static_cast<SOCKET>(clientSocket));
    }

    std::deque<std::shared_ptr<Request>> queued;
    {
        std::lock_guard lock(m_queueMutex);
        queued.swap(m_queue);
    }

    for (const auto& request : queued)
    {
        std::lock_guard requestLock(request->mutex);
        request->result = "Error: Lua runner stopped";
        request->ready = true;
        request->completed.notify_one();
    }
}

bool LuaRunner::QueueRequest(const std::shared_ptr<Request>& request)
{
    std::lock_guard lock(m_queueMutex);
    if (!m_running)
    {
        return false;
    }
    m_queue.push_back(request);
    return true;
}

void LuaRunner::ExecuteQueuedScripts(IScriptSystem* scriptSystem)
{
    if (!m_running)
    {
        return;
    }

    if (!scriptSystem)
    {
        return;
    }

    std::deque<std::shared_ptr<Request>> requests;
    {
        std::lock_guard lock(m_queueMutex);
        requests.swap(m_queue);
    }

    for (const auto& request : requests)
    {
        {
            std::lock_guard requestLock(request->mutex);
            if (request->canceled)
            {
                request->result = request->protocol == Protocol::Kcd2Db
                    ? NativeResponse(false, "request canceled")
                    : "Error: request canceled";
                request->ready = true;
                request->completed.notify_one();
                continue;
            }
            request->started = true;
        }

        bool hadErrors = false;
        std::string messages;

        for (const auto& path : request->paths)
        {
            LogInfo("Lua runner executing: %s", path.c_str());
            if (!ExecuteScriptFile(scriptSystem, path, request->mode))
            {
                hadErrors = true;
                messages += "Error in " + BaseName(path) + ": script execution failed\n";
                LogError("Lua runner failed to execute: %s", path.c_str());
            }
            else
            {
                LogInfo("Lua runner executed: %s", path.c_str());
            }
        }

        if (!hadErrors)
        {
            messages = "All scripts executed successfully";
        }
        if (request->protocol == Protocol::Kcd2Db)
        {
            messages = NativeResponse(!hadErrors, messages);
        }

        {
            std::lock_guard requestLock(request->mutex);
            request->result = messages;
            request->ready = true;
        }
        request->completed.notify_one();
    }
}

void LuaRunner::ServerThread(const std::uint16_t port)
{
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        LogError("Lua runner failed to initialize WinSock.");
        m_running = false;
        return;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
    {
        LogError("Lua runner failed to create socket: %d.", WSAGetLastError());
        WSACleanup();
        m_running = false;
        return;
    }

    m_listenSocket = static_cast<std::uintptr_t>(listenSocket);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (bind(listenSocket, reinterpret_cast<SOCKADDR*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
    {
        LogError("Lua runner failed to bind 127.0.0.1:%u: %d.", port, WSAGetLastError());
        ShutdownAndCloseSocket(listenSocket);
        m_listenSocket = kInvalidSocket;
        WSACleanup();
        m_running = false;
        return;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        LogError("Lua runner failed to listen on 127.0.0.1:%u: %d.", port, WSAGetLastError());
        ShutdownAndCloseSocket(listenSocket);
        m_listenSocket = kInvalidSocket;
        WSACleanup();
        m_running = false;
        return;
    }

    LogInfo("Lua runner listening on 127.0.0.1:%u.", port);

    while (m_running)
    {
        const SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET)
        {
            if (m_running)
            {
                LogWarn("Lua runner accept failed: %d.", WSAGetLastError());
            }
            continue;
        }

        if (!SetClientSocketTimeouts(clientSocket))
        {
            LogWarn("Lua runner failed to set client socket timeouts: %d.", WSAGetLastError());
            ShutdownAndCloseSocket(clientSocket);
            continue;
        }

        {
            std::lock_guard lock(m_socketMutex);
            m_clientSockets.insert(static_cast<std::uintptr_t>(clientSocket));
        }

        while (m_running)
        {
            std::uint32_t messageLength = 0;
            if (!RecvAll(clientSocket, reinterpret_cast<char*>(&messageLength), sizeof(messageLength)))
            {
                break;
            }

            if (messageLength == 0 || messageLength > kMaxMessageLength)
            {
                SendAll(clientSocket, "Error: invalid message length");
                break;
            }

            std::string payload(messageLength, '\0');
            if (!RecvAll(clientSocket, payload.data(), static_cast<int>(payload.size())))
            {
                break;
            }

            std::string immediateResponse;
            auto request = std::make_shared<Request>();
            ParsePayload(std::move(payload), *request, immediateResponse);
            if (!immediateResponse.empty())
            {
                SendAll(clientSocket, immediateResponse);
                continue;
            }

            if (request->paths.empty())
            {
                SendAll(clientSocket, "Error: no script paths received");
                continue;
            }

            if (!QueueRequest(request))
            {
                SendAll(clientSocket, "Error: Lua runner stopped");
                break;
            }
            if (request->protocol == Protocol::Legacy)
            {
                SendAll(clientSocket, "Files queued for execution\n");
            }

            std::unique_lock requestLock(request->mutex);
            if (!request->completed.wait_for(requestLock, kExecutionTimeout, [&request] { return request->ready; }))
            {
                if (!request->started)
                {
                    request->canceled = true;
                }
                requestLock.unlock();
                {
                    std::lock_guard queueLock(m_queueMutex);
                    for (auto it = m_queue.begin(); it != m_queue.end(); ++it)
                    {
                        if (*it == request)
                        {
                            m_queue.erase(it);
                            break;
                        }
                    }
                }
                SendAll(clientSocket, "Error: timed out waiting for game thread");
                continue;
            }
            SendAll(clientSocket, request->result);
        }

        bool shouldCloseClient = false;
        {
            std::lock_guard lock(m_socketMutex);
            shouldCloseClient = m_clientSockets.erase(static_cast<std::uintptr_t>(clientSocket)) > 0;
        }
        if (shouldCloseClient)
        {
            ShutdownAndCloseSocket(clientSocket);
        }
    }

    const auto socketValue = m_listenSocket.exchange(kInvalidSocket);
    if (socketValue != kInvalidSocket)
    {
        ShutdownAndCloseSocket(static_cast<SOCKET>(socketValue));
    }
    WSACleanup();
}
