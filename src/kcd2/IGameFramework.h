// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

/*************************************************************************
   -------------------------------------------------------------------------
   $Id$
   $DateTime$
   Description:	This is the interface which the launcher.exe will interact
                with to start the game framework. For an implementation of
                this interface refer to CryAction.

   -------------------------------------------------------------------------
   History:
   - 20:7:2004   10:34 : Created by Marco Koegler
   - 3:8:2004		11:29 : Taken-over by Márcio Martins

*************************************************************************/

#pragma once

#include "common.h"

struct pe_explosion;
struct IPhysicalEntity;
struct EventPhysRemoveEntityParts;
struct ICombatLog;
struct IAIActorProxy;
struct ICooperativeAnimationManager;
struct IGameSessionHandler;
struct IRealtimeRemoteUpdate;
struct IForceFeedbackSystem;
struct ICommunicationVoiceLibrary;
struct ICustomActionManager;
struct ICustomEventManager;
struct ISerializeHelper;
struct IGameVolumes;

//! Game object extensions need more information than the generic interface can provide.
struct IGameObjectExtension;

struct IGameObjectExtensionCreatorBase
{
};
struct ISystem;
struct IUIDraw;
struct ILanQueryListener;
struct IActor;
struct IActorSystem;
struct IItem;
struct IGameRules;
struct IWeapon;
struct IItemSystem;
struct ILevelSystem;
struct IActionMapManager;
struct IGameChannel;
struct IViewSystem;
struct IVehicle;
struct IVehicleSystem;
struct IGameRulesSystem;
struct IFlowSystem;
struct IGameTokenSystem;
struct IEffectSystem;
struct IGameObject;
struct IGameObjectExtension;
struct IGameObjectSystem;
struct IGameplayRecorder;
struct IAnimationStateNodeFactory;
struct ILoadGame;
struct IGameObject;
struct IMaterialEffects;
struct INetChannel;
struct IPlayerProfileManager;
struct IAnimationGraphState;
struct INetNub;
 struct ISaveGame;

struct IDebugHistoryManager;
struct IDebrisMgr;
struct ISubtitleManager;
struct IDialogSystem;
struct IGameStatistics;
struct ICheckpointSystem;
struct IGameToEditorInterface;
struct IMannequin;
struct IScriptTable;
struct ITimeDemoRecorder;
struct SSystemInitParams;
struct IRenderNode;
struct IEntity;
struct CTimeValue;
struct INetContext;
struct ICrySizer;
struct CrySessionHandle;
struct ICryUnknownPtr;
struct CryInterfaceID;
struct IGeneralMemoryHeap;
template<class T> class CSerializeWrapper;
struct ISerialize;
typedef CSerializeWrapper<ISerialize> TSerialize;

class ISharedParamsManager;

struct INeuralNet;
struct INeuralNetPtr;

enum EGameStartFlags
{
	eGSF_NoLevelLoading        = 0x0001,
	eGSF_Server                = 0x0002,
	eGSF_Client                = 0x0004,
	eGSF_NoDelayedStart        = 0x0008,
	eGSF_BlockingClientConnect = 0x0010,
	eGSF_NoGameRules           = 0x0020,
	eGSF_LocalOnly             = 0x0040,
	eGSF_NoQueries             = 0x0080,
	eGSF_NoSpawnPlayer         = 0x0100,
	eGSF_BlockingMapLoad       = 0x0200,

	eGSF_DemoRecorder          = 0x0400,
	eGSF_DemoPlayback          = 0x0800,

	eGSF_ImmersiveMultiplayer  = 0x1000,
	eGSF_RequireController     = 0x2000,
	eGSF_RequireKeyboardMouse  = 0x4000,

	eGSF_HostMigrated          = 0x8000,
	eGSF_NonBlockingConnect    = 0x10000,
	eGSF_InitClientActor       = 0x20000,
};

enum ESaveGameReason
{
	eSGR_LevelStart,
	eSGR_FlowGraph,
	eSGR_Command,
	eSGR_QuickSave
};

enum ELoadGameResult
{
	eLGR_Ok,
	eLGR_Failed,
	eLGR_FailedAndDestroyedState,
	eLGR_CantQuick_NeedFullLoad
};

static const EntityId LOCAL_PLAYER_ENTITY_ID = 0x7777u; //!< 30583 between static and dynamic EntityIDs.

struct SGameContextParams
{
	const char* levelName;
	const char* gameRules;
	const char* demoRecorderFilename;
	const char* demoPlaybackFilename;

	SGameContextParams()
	{
		levelName = 0;
		gameRules = 0;
		demoRecorderFilename = 0;
		demoPlaybackFilename = 0;
	}
};

struct SGameStartParams
{
};

struct SEntityTagParams
{
};

typedef uint32 THUDWarningId;
struct IGameWarningsListener
{
	// <interfuscator:shuffle>
	virtual ~IGameWarningsListener(){}
	virtual bool OnWarningReturn(THUDWarningId id, const char* returnValue) { return true; }
	virtual void OnWarningRemoved(THUDWarningId id)                         {}
	// </interfuscator:shuffle>
};

//! SRenderNodeCloneLookup is used to associate original IRenderNodes (used in the game) with cloned IRenderNodes, to allow breaks to be played back.
struct SRenderNodeCloneLookup
{
};

//! Provides an interface to game so game will be able to display numeric stats in user-friendly way.
struct IGameStatsConfig
{
	// <interfuscator:shuffle>
	virtual ~IGameStatsConfig(){}
	virtual int         GetStatsVersion() = 0;
	virtual int         GetCategoryMod(const char* cat) = 0;
	virtual const char* GetValueNameByCode(const char* cat, int id) = 0;
	// </interfuscator:shuffle>
};

struct IBreakReplicator
{
	// <interfuscator:shuffle>
	virtual ~IBreakReplicator(){}
	virtual const EventPhysRemoveEntityParts* GetRemovePartEvents(int& iNumEvents) = 0;
	// </interfuscator:shuffle>
};

struct IPersistantDebug
{
};

//! This is the order in which GOEs receive events.
enum EEntityEventPriority
{
	EEntityEventPriority_GameObject = 0,
	EEntityEventPriority_StartAnimProc,
	EEntityEventPriority_AnimatedCharacter,
	EEntityEventPriority_Vehicle,       //!< Vehicles can potentially create move request too!
	EEntityEventPriority_Actor,         //!< Actor must always be higher than AnimatedCharacter.
	EEntityEventPriority_PrepareAnimatedCharacterForUpdate,

	EEntityEventPriority_Last,

	EEntityEventPriority_Client = 100   //!< Special variable for the client to tag onto priorities when needed.
};

//! When you add stuff here, you must also update in CCryAction::Init.
enum EGameFrameworkEvent
{
	eGFE_PauseGame,
	eGFE_ResumeGame,
	eGFE_OnCollision,
	eGFE_OnPostStep,
	eGFE_OnStateChange,
	eGFE_ResetAnimationGraphs,
	eGFE_OnBreakable2d,
	eGFE_OnBecomeVisible,
	eGFE_PreShatter,
	eGFE_BecomeLocalPlayer,
	eGFE_DisablePhysics,
	eGFE_EnablePhysics,
	eGFE_ScriptEvent,
	eGFE_StoodOnChange,
	eGFE_QueueRagdollCreation,  //!< Queue the ragdoll for creation so the engine can do it at the best time.
	eGFE_QueueBlendFromRagdoll, //!< Queue the blend from ragdoll event (i.e. standup).
	eGFE_RagdollPhysicalized,   //!< Dispatched when the queued ragdoll is physicalized.
	eGFE_RagdollUnPhysicalized, //!< Dispatched when the queued ragdoll is unphysicalized (i.e. Stoodup).
	eGFE_EnableBlendRagdoll,    //!< Enable blend with ragdoll mode (will blend with the currently active animation).
	eGFE_DisableBlendRagdoll,   //!< Disable blend with ragdoll (will blend out the ragdoll with the currently active animation).

	eGFE_Last
};

//! All events game should be aware of need to be added here.
enum EActionEvent
{
	eAE_channelCreated,
	eAE_channelDestroyed,
	eAE_connectFailed,
	eAE_connected,
	eAE_disconnected,
	eAE_clientDisconnected,
	eAE_disconnectCommandFinished,
	// Map resetting.
	eAE_resetBegin,
	eAE_resetEnd,
	eAE_resetProgress,
	eAE_preSaveGame,         //!< m_value -> ESaveGameReason.
	eAE_postSaveGame,        //!< m_value -> ESaveGameReason, m_description: 0 (failed), != 0 (successful).
	eAE_inGame,

	eAE_serverName,          //!< Started server.
	eAE_serverIp,            //!< Obtained server ip.
	eAE_earlyPreUpdate,      //!< Called from CryAction's PreUpdate loop after System has been updated, but before subsystems.
	eAE_demoRecorderCreated,
	eAE_mapCmdIssued,
	eAE_unloadLevel,
	eAE_postUnloadLevel,
	eAE_loadLevel,
};

struct SActionEvent
{
	SActionEvent(EActionEvent e, int val = 0, const char* des = 0) :
		m_event(e),
		m_value(val),
		m_description(des)
	{}
	EActionEvent m_event;
	int          m_value;
	const char*  m_description;
};

//! We must take care of order in which listeners are called.
//! Priority order is from low to high.
//! As an example, menu must follow hud as it must be drawn on top of the rest.
enum EFRAMEWORKLISTENERPRIORITY
{
	//! Default priority should not be used unless you don't care about order (it will be called first).
	FRAMEWORKLISTENERPRIORITY_DEFAULT,

	//! Add your order somewhere here if you need to be called between one of them.
	FRAMEWORKLISTENERPRIORITY_GAME,
	FRAMEWORKLISTENERPRIORITY_HUD,
	FRAMEWORKLISTENERPRIORITY_MENU
};

struct ISaveGame
{
	virtual ~ISaveGame(){}
	// initialize - set output path
	virtual bool Init(const char* name) = 0;

	// set some basic meta-data
	virtual void       AddMetadata(const char* tag, const char* value) = 0;
	virtual void       AddMetadata(const char* tag, int value) = 0;
	// create a serializer for some data section
	virtual TSerialize AddSection(const char* section) = 0;
	// set a thumbnail.
	// if imageData == 0: only reserves memory and returns ptr to local data
	// if imageData != 0: copies data from imageData into local buffer
	// imageData is in BGR or BGRA
	// returns ptr to internal data storage (size=width*height*depth) if Thumbnail supported,
	// 0 otherwise
	virtual uint8* SetThumbnail(const uint8* imageData, int width, int height, int depth) = 0;

	// set a thumbnail from an already present bmp file
	// file will be read on function call
	// returns true if successful, false otherwise
	virtual bool SetThumbnailFromBMP(const char* filename) = 0;

	// finish - indicate success (negative success *must* remove file)
	// also calls delete this;
	virtual bool Complete(bool successfulSoFar) = 0;

	// returns the filename of this savegame
	virtual const char* GetFileName() const = 0;

	// save game reason
	virtual void            SetSaveGameReason(ESaveGameReason reason) = 0;
	virtual ESaveGameReason GetSaveGameReason() const = 0;

	virtual void            GetMemoryUsage(ICrySizer* pSizer) const = 0;
};
struct ILoadGame
{
	virtual ~ILoadGame(){}
	// initialize - set name of game
	virtual bool                Init(const char* name) = 0;

	virtual IGeneralMemoryHeap* GetHeap() = 0;

	// get some basic meta-data
	virtual const char*                 GetMetadata(const char* tag) = 0;
	virtual bool                        GetMetadata(const char* tag, int& value) = 0;
	virtual bool                        HaveMetadata(const char* tag) = 0;
	// create a serializer for some data section
	virtual void* GetSection(const char* section) = 0;
	virtual bool                        HaveSection(const char* section) = 0;

	// finish - indicate success (negative success *must* remove file)
	// also calls delete this;
	virtual void Complete() = 0;

	// returns the filename of this savegame
	virtual const char* GetFileName() const = 0;
};


struct IGameFrameworkListener
{
	virtual ~IGameFrameworkListener(){}
	virtual void OnPostUpdate(float fDeltaTime) = 0;
	virtual void OnSaveGame(ISaveGame* pSaveGame) = 0;
	virtual void OnLoadGame(ILoadGame* pLoadGame) = 0;
	virtual void OnLevelEnd(const char* nextLevel) = 0;
	virtual void OnActionEvent(const SActionEvent& event) = 0;
	virtual void OnPreRender() {}

	//! Called when the savegame data is in memory, but before the procesing of it starts.
	virtual void OnSavegameFileLoadedInMemory(const char* pLevelName) {}
	virtual void OnForceLoadingWithFlash()                            {}
};

struct IBreakEventListener
{
};

struct TimerCallback;
//! Interface which exposes the CryAction subsystems.
struct IGameFramework
{
	typedef uint32                   TimerID;
   virtual void pad0() = 0;
   virtual void pad1() = 0;
   virtual void pad2() = 0;
   virtual void pad3() = 0;
   virtual void pad4() = 0;

	virtual ~IGameFramework(){}

	//! Entry function to the game framework.
	//! Entry function used to create a new instance of the game framework from outside its own DLL.
	//! \return New instance of the game framework.
	typedef IGameFramework*(* TEntryFunction)();

	//! Initialize CRYENGINE with every system needed for a general action game.
	//! Independently of the success of this method, Shutdown must be called.
	//! \param startupParams Pointer to SSystemInitParams structure containing system initialization setup!
	//! \return 0 if something went wrong with initialization, non-zero otherwise.
	virtual bool Init(SSystemInitParams& startupParams) = 0;

	//! Used to notify the framework that we're switching between single and multi player.
	virtual void InitGameType(bool multiplayer, bool fromInit) = 0;

	//! Complete initialization of game framework with things that can only be done after all entities have been registered.
	virtual bool CompleteInit() = 0;

	//! Shuts down CryENGINE and any other subsystem created during initialization.
	virtual void Shutdown() = 0;

	//! Updates CRYENGINE before starting a game frame.
	//! \param[in] haveFocus true if the game has the input focus.
	//! \param[in] updateFlags - Flags specifying how to update.
	//! \return 0 if something went wrong with initialization, non-zero otherwise.
	virtual bool PreUpdate(bool haveFocus, unsigned int updateFlags) = 0;

	//! Updates CRYENGINE after a game frame.
	//! \param[in] haveFocus true if the game has the input focus.
	//! \param[in] updateFlags Flags specifying how to update.
	virtual void PostUpdate(bool haveFocus, unsigned int updateFlags) = 0;

	//! Resets the current game
	virtual void Reset(bool clients) = 0;

	//! Pauses the game
	//! \param pause true if the game is pausing, false otherwise.
	//! \param nFadeOutInMS Time SFX and Voice will be faded out over in MilliSec.
	virtual void PauseGame(bool pause, bool force, unsigned int nFadeOutInMS = 0) = 0;

	//! Returns the pause status
	//! \return true if the game is paused, false otherwise.
	virtual bool IsGamePaused() = 0;

	//! Are we completely into game mode?
	virtual bool IsGameStarted() = 0;

	//! Check if the game is allowed to start the actual gameplay.
	virtual bool IsLevelPrecachingDone() const = 0;

	//! Inform game that it is allowed to start the gameplay.
	virtual void SetLevelPrecachingDone(bool bValue) = 0;

	//! \return Pointer to the ISystem interface.
	virtual ISystem* GetISystem() = 0;

	//! \return Pointer to the ILanQueryListener interface.
	virtual ILanQueryListener* GetILanQueryListener() = 0;

	//! \return Pointer to the IUIDraw interface.
	virtual IUIDraw*    GetIUIDraw() = 0;

	virtual IMannequin& GetMannequinInterface() = 0;

	//! Returns a pointer to the IGameObjectSystem interface.
	//! \return Pointer to IGameObjectSystem interface.
	virtual IGameObjectSystem* GetIGameObjectSystem() = 0;

	//! Returns a pointer to the ILevelSystem interface.
	//! \return Pointer to ILevelSystem interface.
	virtual ILevelSystem* GetILevelSystem() = 0;

	//! Returns a pointer to the IActorSystem interface.
	//! \return Pointer to IActorSystem interface.
	virtual IActorSystem* GetIActorSystem() = 0;

	//! Returns a pointer to the IItemSystem interface.
	//! \return Pointer to IItemSystem interface.
	virtual IItemSystem* GetIItemSystem() = 0;

	//! Returns a pointer to the IBreakReplicator interface.
	//! \return Pointer to IBreakReplicator interface.
	virtual IBreakReplicator* GetIBreakReplicator() = 0;

	//! Returns a pointer to the IActionMapManager interface.
	//! \return Pointer to IActionMapManager interface.
	virtual IActionMapManager* GetIActionMapManager() = 0;

	//! Returns a pointer to the IViewSystem interface.
	//! \return Pointer to IViewSystem interface.
	virtual IViewSystem* GetIViewSystem() = 0;

	//! Returns a pointer to the IGameplayRecorder interface.
	//! \return Pointer to IGameplayRecorder interface.
	virtual IGameplayRecorder* GetIGameplayRecorder() = 0;

	//! Returns a pointer to the IVehicleSystem interface.
	//! \return Pointer to IVehicleSystem interface.
	virtual IVehicleSystem* GetIVehicleSystem() = 0;

	//! Returns a pointer to the IGameRulesSystem interface.
	//! \return Pointer to IGameRulesSystem interface.
	virtual IGameRulesSystem* GetIGameRulesSystem() = 0;

	//! Returns a pointer to the IFlowSystem interface.
	//! \return Pointer to IFlowSystem interface.
	virtual IFlowSystem* GetIFlowSystem() = 0;

	//! Returns a pointer to the IGameTokenSystem interface
	//! \return Pointer to IGameTokenSystem interface.
	virtual IGameTokenSystem* GetIGameTokenSystem() = 0;

	//! Returns a pointer to the IEffectSystem interface
	//! \return Pointer to IEffectSystem interface.
	virtual IEffectSystem* GetIEffectSystem() = 0;

	//! Returns a pointer to the IMaterialEffects interface.
	//! \return Pointer to IMaterialEffects interface.
	virtual IMaterialEffects* GetIMaterialEffects() = 0;

	//! Returns a pointer to the IDialogSystem interface
	//! \return Pointer to IDialogSystem interface.
	virtual IDialogSystem* GetIDialogSystem() = 0;

	//! Returns a pointer to the IPlayerProfileManager interface.
	//! \return Pointer to IPlayerProfileManager interface.
	virtual IPlayerProfileManager* GetIPlayerProfileManager() = 0;

	//! Returns a pointer to the ISubtitleManager interface.
	//! \return Pointer to ISubtitleManager interface.
	virtual ISubtitleManager* GetISubtitleManager() = 0;

	//! Returns a pointer to the IRealtimeUpdate Interface.
	virtual IRealtimeRemoteUpdate* GetIRealTimeRemoteUpdate() = 0;

	//! Returns a pointer to the IGameStatistics interface.
	virtual IGameStatistics* GetIGameStatistics() = 0;

	//! Pointer to ICooperativeAnimationManager interface.
	virtual ICooperativeAnimationManager* GetICooperativeAnimationManager() = 0;

	//! Pointer to ICheckpointSystem interface.
	virtual ICheckpointSystem* GetICheckpointSystem() = 0;

	//! Pointer to IForceFeedbackSystem interface.
	virtual IForceFeedbackSystem* GetIForceFeedbackSystem() const = 0;

	//! Pointer to ICustomActionManager interface.
	virtual ICustomActionManager* GetICustomActionManager() const = 0;

	//! Pointer to ICustomEventManager interface.
	virtual ICustomEventManager* GetICustomEventManager() const = 0;

	virtual IGameSessionHandler* GetIGameSessionHandler() = 0;

	//! Get pointer to Shared Parameters manager interface class.
	virtual ISharedParamsManager* GetISharedParamsManager() = 0;

	//! Initialises a game context.
	//! \param pGameStartParams Parameters for configuring the game.
	//! \return true if successful, false otherwise.
	virtual bool StartGameContext(const SGameStartParams* pGameStartParams) = 0;

	//! Changes a game context (levels and rules, etc); only allowed on the server.
	//! \param pGameContextParams Parameters for configuring the context.
	//! \return true if successful, false otherwise.
	virtual bool ChangeGameContext(const SGameContextParams* pGameContextParams) = 0;

	//! Finished a game context (no game running anymore).
	virtual void EndGameContext() = 0;

	//! Detect if a context is currently running.
	//! \return true if a game context is running.
	virtual bool StartedGameContext() const = 0;

	//! Detect if a context is currently starting.
	//! \return true if a game context is starting.
	virtual bool StartingGameContext() const = 0;

	//! Sets the current game session handler to another implementation.
	virtual void SetGameSessionHandler(IGameSessionHandler* pSessionHandler) = 0;

	//! For the editor: spawn a player and wait for connection
	virtual bool BlockingSpawnPlayer() = 0;

	//! Remove broken entity parts
	virtual void FlushBreakableObjects() = 0;

	//! For the game : fix the broken game objects (to restart the map)
	virtual void ResetBrokenGameObjects() = 0;

	//! For the kill cam : clone the list of objects specified in the break events indexed
	virtual void CloneBrokenObjectsAndRevertToStateAtTime(int32 iFirstBreakEventIndex, uint16* pBreakEventIndices, int32& iNumBreakEvents, IRenderNode** outClonedNodes, int32& iNumClonedNodes, SRenderNodeCloneLookup& renderNodeLookup) = 0;

	//! For the kill cam: apply a single break event from an index
	virtual void ApplySingleProceduralBreakFromEventIndex(uint16 uBreakEventIndex, const SRenderNodeCloneLookup& renderNodeLookup) = 0;

	//! For the game: unhide the broken game objects (at the end of the kill cam)
	virtual void UnhideBrokenObjectsByIndex(uint16* ObjectIndicies, int32 iNumObjectIndices) = 0;

	//! Let the GameFramework initialize with the editor
	virtual void InitEditor(IGameToEditorInterface* pGameToEditor) = 0;

	//! Inform the GameFramework of the current level loaded in the editor.
	virtual void SetEditorLevel(const char* levelName, const char* levelFolder) = 0;

	//! Retrieves the current level loaded by the editor.
	//! Parameters are pointers to receive the level infos.
	virtual void GetEditorLevel(char** levelName, char** levelFolder) = 0;

	//! Begin a query on the LAN for games
	virtual void BeginLanQuery() = 0;

	//! End the current game query
	virtual void EndCurrentQuery() = 0;

	//! Returns the Actor associated with the client (or NULL)
	virtual IActor* GetClientActor() const = 0;

	//! Returns the Actor Id associated with the client (or NULL)
	virtual EntityId GetClientActorId() const = 0;

	//! Returns the Entity associated with the client (or NULL)
	virtual IEntity* GetClientEntity() const = 0;

	//! Returns the EntityId associated with the client (or NULL)
	virtual EntityId GetClientEntityId() const = 0;

	//! Returns the INetChannel associated with the client (or NULL)
	virtual INetChannel* GetClientChannel() const = 0;

	//! Wrapper for INetContext::DelegateAuthority()
	virtual void DelegateAuthority(EntityId entityId, uint16 channelId) = 0;

	//! Returns the (synched) time of the server (so use this for timed events, such as MP round times)
	virtual CTimeValue GetServerTime() = 0;

	//! Retrieve the Game Server Channel Id associated with the specified INetChannel.
	//! \return The Game Server ChannelId associated with the specified INetChannel.
	virtual uint16 GetGameChannelId(INetChannel* pNetChannel) = 0;

	//! Check if the game server channel has lost connection but still on hold and able to recover...
	//! \return true if the specified game server channel has lost connection but it's stil able to recover...
	virtual bool IsChannelOnHold(uint16 channelId) = 0;

	//! Retrieve a pointer to the INetChannel associated with the specified Game Server Channel Id.
	//! \return Pointer to INetChannel associated with the specified Game Server Channel Id.
	virtual INetChannel* GetNetChannel(uint16 channelId) = 0;

	//! Retrieve an IGameObject from an entity id
	//! \return Pointer to IGameObject of the entity if it exists (or NULL otherwise)
	virtual IGameObject* GetGameObject(EntityId id) = 0;

	//! Retrieve a network safe entity class id, that will be the same in client and server
	//! \return true if an entity class with this name has been registered
	virtual bool GetNetworkSafeClassId(uint16& id, const char* className) = 0;

	//! Retrieve a network safe entity class name, that will be the same in client and server
	//! \return true if an entity class with this id has been registered
	virtual bool GetNetworkSafeClassName(char* className, size_t maxn, uint16 id) = 0;

	//! Retrieve an IGameObjectExtension by name from an entity
	//! \return Pointer to IGameObjectExtension of the entity if it exists (or NULL otherwise)
	virtual IGameObjectExtension* QueryGameObjectExtension(EntityId id, const char* name) = 0;

	//! Retrieve pointer to the ITimeDemoRecorder (or NULL)
	virtual ITimeDemoRecorder* GetITimeDemoRecorder() const = 0;

	//! Save the current game to disk
	virtual bool SaveGame(const char* path, bool quick = false, bool bForceImmediate = true, ESaveGameReason reason = eSGR_QuickSave, bool ignoreDelay = false, const char* checkPoint = NULL) = 0;

	//! Load a game from disk (calls StartGameContext...)
	virtual ELoadGameResult LoadGame(const char* path, bool quick = false, bool ignoreDelay = false) = 0;

	//! Schedules the level load for the next level
	virtual void ScheduleEndLevelNow(const char* nextLevel) = 0;

	//! Notification that game mode is being entered/exited
	//! iMode values: 0-leave game mode, 1-enter game mode, 3-leave AI/Physics mode, 4-enter AI/Physics mode
	virtual void OnEditorSetGameMode(int iMode) = 0;

	virtual bool IsEditing() = 0;

	virtual bool IsInLevelLoad() = 0;

	virtual bool IsLoadingSaveGame() = 0;

	virtual bool IsInTimeDemo() = 0;
	virtual bool IsTimeDemoRecording() = 0;

	virtual void AllowSave(bool bAllow = true) = 0;
	virtual void AllowLoad(bool bAllow = true) = 0;
	virtual bool CanSave() = 0;
	virtual bool CanLoad() = 0;

	//! Gets a serialization helper for read/write usage based on settings
	virtual ISerializeHelper* GetSerializeHelper() const = 0;

	//! Check if the current game can activate cheats (flymode, godmode, nextspawn)
	virtual bool CanCheat() = 0;

	//! \return Path relative to the levels folder e.g. "Multiplayer\PS\Shore".
	virtual const char* GetLevelName() = 0;

	// \return pPathBuffer[0] == 0 if no level is loaded.
	virtual void              GetAbsLevelPath(char* pPathBuffer, uint32 pathBufferSize) = 0;

	virtual IPersistantDebug* GetIPersistantDebug() = 0;

	//! Adds a listener for break events.
	virtual void AddBreakEventListener(IBreakEventListener* pListener) = 0;

	//! Removes a listener for break events.
	virtual void                  RemoveBreakEventListener(IBreakEventListener* pListener) = 0;
	// 第101个虚函数
	virtual void                  RegisterListener(IGameFrameworkListener* pGameFrameworkListener, const char* name, EFRAMEWORKLISTENERPRIORITY eFrameworkListenerPriority) = 0;
	virtual void                  UnregisterListener(IGameFrameworkListener* pGameFrameworkListener) = 0;

	virtual INetNub*              GetServerNetNub() = 0;
	virtual INetNub*              GetClientNetNub() = 0;

	virtual void                  SetGameGUID(const char* gameGUID) = 0;
	virtual const char*           GetGameGUID() = 0;
	virtual INetContext*          GetNetContext() = 0;

	virtual void                  GetMemoryUsage(ICrySizer* pSizer) const = 0;

	virtual void                  EnableVoiceRecording(const bool enable) = 0;

	virtual void                  MutePlayerById(EntityId mutePlayer) = 0;

	virtual IDebugHistoryManager* CreateDebugHistoryManager() = 0;

	virtual void                  DumpMemInfo(const char* format, ...) = 0;

	//! Check whether the client actor is using voice communication.
	virtual bool IsVoiceRecordingEnabled() = 0;

	virtual bool IsImmersiveMPEnabled() = 0;

	//! Executes console command on next frame's beginning
	virtual void        ExecuteCommandNextFrame(const char*) = 0;

	virtual const char* GetNextFrameCommand() const = 0;

	virtual void        ClearNextFrameCommand() = 0;

	//! Opens a page in default browser.
	virtual void ShowPageInBrowser(const char* URL) = 0;

	//! Opens a page in default browser.
	virtual bool StartProcess(const char* cmd_line) = 0;

	//! Saves dedicated server console variables in server config file.
	virtual bool SaveServerConfig(const char* path) = 0;

	//! To avoid stalls during gameplay and to get a list of all assets needed for the level (bEnforceAll=true).
	//! \param bEnforceAll true to ensure all possible assets become registered (list should not be too conservative - to support level stripification).
	virtual void PrefetchLevelAssets(const bool bEnforceAll) = 0;

	virtual void ReleaseGameStats() = 0;

	//! Inform that an IEntity was spawned from breakage.
	virtual void OnBreakageSpawnedEntity(IEntity* pEntity, IPhysicalEntity* pPhysEntity, IPhysicalEntity* pSrcPhysEntity) = 0;

	//! Returns true if the supplied game session is a game session.
	virtual bool IsGameSession(CrySessionHandle sessionHandle) = 0;

	//! Returns true if the nub should be migrated for a given session.
	virtual bool ShouldMigrateNub(CrySessionHandle sessionHandle) = 0;

	//! Adds a timer that will trigger a callback function passed by parameter.
	//! Allows to pass some user data pointer that will be one of the parameters for the callback function.
	//! The signature for the callback function is: void (void*, int).
	//! It allows member functions by using CE functors.
	//! \return Handle of the timer created
	virtual IGameFramework::TimerID AddTimer(CTimeValue interval, bool repeat, TimerCallback callback, void* userdata = 0) = 0;

	//! Remove an existing timer by using its handle, returns user data.
	virtual void* RemoveTimer(TimerID timerID) = 0;

	//! Return ticks last preupdate took.
	virtual uint32 GetPreUpdateTicks() = 0;

	//! Get the time left when we are allowed to load a new game.
	//! When this returns 0, we are allowed to load a new game.
	virtual float GetLoadSaveDelay() const = 0;

	//! Allows the network code to keep ticking in the event of a stall on the main thread.
	virtual void StartNetworkStallTicker(bool includeMinimalUpdate) = 0;
	virtual void StopNetworkStallTicker() = 0;

	//! Retrieves manager which handles game objects tied to editor shapes and volumes.
	virtual IGameVolumes* GetIGameVolumesManager() const = 0;

	virtual void          PreloadAnimatedCharacter(IScriptTable* pEntityScript) = 0;

	//! Gets called from the physics thread just before doing a time step.
	//! \param deltaTime - the time interval that will be simulated.
	virtual void PrePhysicsTimeStep(float deltaTime) = 0;

	//! Register an extension to the game framework and makes it accessible through it
	//! \param pExtension Extension to be added to the game framework.
	virtual void RegisterExtension(ICryUnknownPtr pExtension) = 0;

	virtual void ReleaseExtensions() = 0;

protected:
	//! Retrieves an extension interface by interface id.
	//! Internal, client uses 'QueryExtension<ExtensionInterface>()
	//! \param interfaceID Interface id.
	virtual ICryUnknownPtr QueryExtensionInterfaceById(const CryInterfaceID& interfaceID) const = 0;

	// </interfuscator:shuffle>
};
