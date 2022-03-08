#pragma once


#ifdef RUNTIME

#include <string>

#include "ArrayVar.h"
#include "LambdaManager.h"
#include "PluginAPI.h"
#include <variant>

class Script;
class TESForm;
class TESObjectREFR;
class BGSListForm;
class Actor;
typedef void (*EventHookInstaller)();

// For dispatching events to scripts.
// Scripts can register an event handler for any of the supported events.
// Can optionally specify filters to match against the event arguments.
// Event handler is a function script which must take the expected number and types of arguments associated with the event.
// Supporting hooks only installed if at least one handler is registered for a particular event.


namespace EventManager
{
	extern Stack<const char*> s_eventStack;

	static constexpr auto numMaxFilters = 0x20;

	using EventHandler = NVSEEventManagerInterface::EventHandler;
	using DispatchCallback = NVSEEventManagerInterface::DispatchCallback;
	using EventFlags = NVSEEventManagerInterface::EventFlags;
	using DispatchReturn = NVSEEventManagerInterface::DispatchReturn;

	enum eEventID {
		// correspond to ScriptEventList event masks
		kEventID_OnAdd,
		kEventID_OnActorEquip,
		kEventID_OnDrop,
		kEventID_OnActorUnequip,
		kEventID_OnDeath,
		kEventID_OnMurder,
		kEventID_OnCombatEnd,
		kEventID_OnHit,
		kEventID_OnHitWith,
		kEventID_OnPackageChange,
		kEventID_OnPackageStart,
		kEventID_OnPackageDone,
		kEventID_OnLoad,
		kEventID_OnMagicEffectHit,
		kEventID_OnSell,
		kEventID_OnStartCombat,
		kEventID_SayToDone,
		kEventID_OnGrab,
		kEventID_OnOpen,
		kEventID_OnClose,
		kEventID_OnFire,
		kEventID_OnTrigger,
		kEventID_OnTriggerEnter,
		kEventID_OnTriggerLeave,
		kEventID_OnReset,

		kEventID_ScriptEventListMAX,

		// special-cased game events
		kEventID_OnActivate = kEventID_ScriptEventListMAX,
		kEventID_OnDropItem,

		kEventID_GameEventMAX,

		// NVSE internal events, correspond to NVSEMessagingInterface messages
		kEventID_ExitGame = kEventID_GameEventMAX,
		kEventID_ExitToMainMenu,
		kEventID_LoadGame,
		kEventID_SaveGame,
		kEventID_QQQ,
		kEventID_PostLoadGame,
		kEventID_RuntimeScriptError,
		kEventID_DeleteGame,
		kEventID_RenameGame,
		kEventID_RenameNewGame,
		kEventID_NewGame,
		kEventID_DeleteGameName,
		kEventID_RenameGameName,
		kEventID_RenameNewGameName,

		kEventID_InternalMAX,

		// user-defined
		kEventID_UserDefinedMIN = kEventID_InternalMAX,

		kEventID_INVALID = 0xFFFFFFFF
	};

	struct EventInfo;

	//If variant is Maybe_Lambda, must try to capture lambda context once the EventCallback is confirmed to stay. 
	using CallbackFunc = std::variant<LambdaManager::Maybe_Lambda, EventHandler>;

	//Call the callback...
	std::unique_ptr<ScriptToken> Invoke(const CallbackFunc &func, EventInfo* eventInfo, void* arg0, void* arg1);
	std::unique_ptr<ScriptToken> InvokeRaw(const CallbackFunc &func, EventInfo& eventInfo, void* args, TESObjectREFR* thisObj);

	// Represents an event handler registered for an event.
	class EventCallbackInfo
	{
	public:
		EventCallbackInfo() = default;
		~EventCallbackInfo() = default;

		EventCallbackInfo(const EventCallbackInfo& other) = delete;
		EventCallbackInfo& operator=(const EventCallbackInfo& other) = delete;

		EventCallbackInfo(EventCallbackInfo&& other) noexcept;
		EventCallbackInfo& operator=(EventCallbackInfo&& other) noexcept;

		TESForm			*source{};				// first arg to handler (reference or base form or form list)
		TESForm			*object{};				// second arg to handler
		bool			removed{};
		bool			pendingRemove{};

		using Index = UInt32;
		using Filter = SelfOwningArrayElement;

		//Indexes for filters must respect the max amount of BaseFilters for the base event definition.
		//If no filter is at an index = it is unfiltered for the nth BaseFilter.
		//Using a map to avoid adding duplicate indexes.
		std::map<Index, Filter> filters;

		[[nodiscard]] bool IsRemoved() const { return removed; }
		void SetRemoved(bool bSet) { removed = bSet; }
		void Remove(EventInfo* eventInfo, LinkedList<EventCallbackInfo>::Iterator& iter);

		[[nodiscard]] bool Equals(const EventCallbackInfo& rhs) const;	// compare, return true if the two handlers are identical
	};

	struct EventCallback : std::pair<CallbackFunc, EventCallbackInfo>
	{
		using Parent = std::pair<CallbackFunc, EventCallbackInfo>;

		void TrySaveLambdaContext();

		[[nodiscard]] Script* TryGetScript() const;

		//If the EventCallback is confirmed to stay, then call this to wrap up loose ends, e.g save lambda var context.
		void Confirm();
	};

	using EventCallbackMap = std::multimap<CallbackFunc, EventCallbackInfo>;

	struct EventInfo
	{
		EventInfo(const char* name_, Script::VariableType* paramsTypes_, UInt8 nParams_, UInt32 eventMask_, EventHookInstaller* installer_, NVSEEventManagerInterface::EventFlags flags_)
			: evName(name_), paramTypes(paramsTypes_), numParams(nParams_), eventMask(eventMask_), installHook(installer_), flags(flags_)
		{}

		EventInfo(const char* name_, Script::VariableType* paramsTypes_, UInt8 numParams_) : evName(name_), paramTypes(paramsTypes_), numParams(numParams_), eventMask(0), installHook(nullptr) {}

		EventInfo() : evName(""), paramTypes(nullptr), numParams(0), eventMask(0), installHook(nullptr) {}

		EventInfo(const EventInfo& other) = default;

		EventInfo& operator=(const EventInfo& other)
		{
			if (this == &other)
				return *this;
			evName = other.evName;
			paramTypes = other.paramTypes;
			numParams = other.numParams;
			callbacks = other.callbacks;
			eventMask = other.eventMask;
			installHook = other.installHook;
			return *this;
		}

		const char* evName;			// must be lowercase
		Script::VariableType* paramTypes;
		UInt8				numParams;
		UInt32				eventMask;
		EventCallbackMap	callbacks;
		EventHookInstaller* installHook;	// if a hook is needed for this event type, this will be non-null. 
											// install it once and then set *installHook to NULL. Allows multiple events
											// to use the same hook, installing it only once.

		NVSEEventManagerInterface::EventFlags flags = NVSEEventManagerInterface::kFlags_None;

		[[nodiscard]] bool FlushesOnLoad() const
		{
			return flags & NVSEEventManagerInterface::kFlag_FlushOnLoad;
		}
	};

	using EventInfoList = Vector<EventInfo>;
	static EventInfoList s_eventInfos(0x30);


	bool SetHandler(const char* eventName, EventCallbackInfo& handler);

	// removes handler only if all filters match
	bool RemoveHandler(const char* id, const EventCallbackInfo& handler);

	// handle an NVSEMessagingInterface message
	void HandleNVSEMessage(UInt32 msgID, void* data);

	// handle an eventID directly
	void __stdcall HandleEvent(UInt32 id, void * arg0, void * arg1);

	// name of whatever event is currently being handled, empty string if none
	const char* GetCurrentEventName();

	// called each frame to update internal state
	void Tick();

	void Init();

	bool RegisterEventEx(const char* name, UInt8 numParams, Script::VariableType* paramTypes, UInt32 eventMask = 0, 
		EventHookInstaller* hookInstaller = nullptr, 
		EventFlags flags = EventFlags::kFlags_None);

	bool RegisterEvent(const char* name, UInt8 numParams, Script::VariableType* paramTypes, EventFlags flags);
	bool SetNativeEventHandler(const char* eventName, EventHandler func);
	bool RemoveNativeEventHandler(const char* eventName, EventHandler func);

	DispatchReturn DispatchEvent(const char* eventName, DispatchCallback resultCallback, TESObjectREFR* thisObj, ...);

	// dispatch a user-defined event from a script
	bool DispatchUserDefinedEvent (const char* eventName, Script* sender, UInt32 argsArrayId, const char* senderName);
};


#endif