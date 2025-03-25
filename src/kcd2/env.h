#ifndef ENV_H
#define ENV_H
#include "console.h"

struct IScriptSystem;
struct ISystem;
struct IEntitySystem;
struct IGame;

struct SSystemGlobalEnvironment
{
    void* pDialogSystem;
    void* p3DEngine;
    void* pNetwork;
    void* pOnline;
    void* pLobby;
    IScriptSystem* pScriptSystem;
    void* pPhysicalWorld;
    void* pFlowSystem;
    void* pInput;
    void* pStatoscope;
    void* pCryPak;
    void* pFileChangeMonitor;
    void* pProfileLogSystem;
    void* pParticleManager;
    void* pOpticsManager;
    void* pFrameProfileSystem;
    void* pTimer;
    void* pCryFont;
    IGame* pGame;
    void* pLocalMemoryUsage;
    IEntitySystem* pEntitySystem;
    IConsole* pConsole;
    void* pAudioSystem;
    ISystem* pSystem;
    void* pCharacterManager;
    void* pAISystem;
    // ...

    SSystemGlobalEnvironment* operator->()
    {
        return this;
    }

    SSystemGlobalEnvironment& operator*()
    {
        return *this;
    }

    bool operator!() const
    {
        return false;
    }

    explicit operator bool() const
    {
        return true;
    }
};


SSystemGlobalEnvironment gEnv;

#endif //ENV_H
