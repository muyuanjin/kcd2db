#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

struct IScriptSystem;

class LuaRunner final
{
public:
    static LuaRunner& Instance();

    bool StartFromCommandLine();
    void Stop();
    void ExecuteQueuedScripts(IScriptSystem* scriptSystem);

    enum class Protocol
    {
        Legacy,
        Kcd2Db,
    };

    enum class ExecutionMode
    {
        Auto,
        Buffer,
        File,
    };

    struct Request
    {
        std::vector<std::string> paths;
        Protocol protocol = Protocol::Legacy;
        ExecutionMode mode = ExecutionMode::Auto;
        std::mutex mutex;
        std::condition_variable completed;
        std::string result;
        bool ready = false;
        bool started = false;
        bool canceled = false;
    };

private:
    LuaRunner() = default;
    ~LuaRunner() = default;
    LuaRunner(const LuaRunner&) = delete;
    LuaRunner& operator=(const LuaRunner&) = delete;

    void ServerThread(std::uint16_t port);
    bool QueueRequest(const std::shared_ptr<Request>& request);

    std::mutex m_queueMutex;
    std::deque<std::shared_ptr<Request>> m_queue;
    std::mutex m_socketMutex;
    std::unordered_set<std::uintptr_t> m_clientSockets;
    std::atomic_bool m_running{false};
    std::atomic<std::uintptr_t> m_listenSocket{~std::uintptr_t{0}};
};
