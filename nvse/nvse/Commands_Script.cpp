#include "Commands_Script.h"

#include <filesystem>

#include "ParamInfos.h"
#include "GameScript.h"
#include "MemoizedMap.h"
#include "ScriptUtils.h"
#include "PluginManager.h"
#include "ScriptAnalyzer.h"

#if RUNTIME
#include "GameAPI.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameData.h"

#include "EventManager.h"
#include "FunctionScripts.h"
#include <fstream>
//#include "ModTable.h"

enum EScriptMode
{
	eScript_HasScript,
	eScript_Get,
	eScript_Remove,
};

static bool GetScript_Execute(COMMAND_ARGS, EScriptMode eMode)
{
	*result = 0;
	TESForm *form = nullptr;

	ExtractArgsEx(EXTRACT_ARGS_EX, &form);
	const bool parmForm = form ? true : false;

	form = form->TryGetREFRParent();
	if (!form)
	{
		if (!thisObj)
			return true;
		form = thisObj->baseForm;
	}

	const auto scriptForm = DYNAMIC_CAST(form, TESForm, TESScriptableForm);
	Script *script = nullptr;
	EffectSetting *effect = nullptr;
	if (!scriptForm) // Let's try for a MGEF
	{
		effect = DYNAMIC_CAST(form, TESForm, EffectSetting);
		if (effect)
			script = effect->GetScript();
	}
	else
		script = (scriptForm) ? scriptForm->script : nullptr;

	switch (eMode)
	{
	case eScript_HasScript:
	{
		*result = (script != nullptr) ? 1 : 0;
		break;
	}
	case eScript_Get:
	{
		if (script)
		{
			const auto refResult = (UInt32 *)result;
			*refResult = script->refID;
		}
		break;
	}
	case eScript_Remove:
	{
		// simply forget about the script
		if (script)
		{
			const auto refResult = (UInt32 *)result;
			*refResult = script->refID;
		}
		if (scriptForm)
			scriptForm->script = nullptr;
		else if (effect)
			effect->RemoveScript();
		if (!parmForm && thisObj)
		{
			// Remove ExtraScript entry - otherwise the script will keep running until the reference is reloaded.
			thisObj->extraDataList.RemoveByType(kExtraData_Script);
		}
		break;
	}
	}
	return true;
}

bool Cmd_IsScripted_Execute(COMMAND_ARGS)
{
	return GetScript_Execute(PASS_COMMAND_ARGS, eScript_HasScript);
}

bool Cmd_GetScript_Execute(COMMAND_ARGS)
{
	return GetScript_Execute(PASS_COMMAND_ARGS, eScript_Get);
}

bool Cmd_RemoveScript_Execute(COMMAND_ARGS)
{
	return GetScript_Execute(PASS_COMMAND_ARGS, eScript_Remove);
}

bool Cmd_SetScript_Execute(COMMAND_ARGS)
{
	*result = 0;
	const auto refResult = (UInt32 *)result;

	TESForm *form = nullptr;
	TESForm *pForm = nullptr;
	TESForm *scriptArg = nullptr;

	ExtractArgsEx(EXTRACT_ARGS_EX, &scriptArg, &form);
	const bool parmForm = form ? true : false;

	form = form->TryGetREFRParent();
	if (!form)
	{
		if (!thisObj)
			return true;
		form = thisObj->baseForm;
	}

	const auto scriptForm = DYNAMIC_CAST(form, TESForm, TESScriptableForm);
	Script *oldScript = nullptr;
	EffectSetting *effect = nullptr;
	if (!scriptForm) // Let's try for a MGEF
	{
		effect = DYNAMIC_CAST(form, TESForm, EffectSetting);
		if (effect)
			oldScript = effect->GetScript();
		else
			return true;
	}
	else
		oldScript = scriptForm->script;

	const auto script = DYNAMIC_CAST(scriptArg, TESForm, Script);
	if (!script)
		return true;

	// we can only get a magic script here for an EffectSetting
	if (script->IsMagicScript() && !effect)
		return true;

	// we can't get an unknown script here
	if (script->IsUnkScript())
		return true;

	if (script->IsMagicScript())
	{
		effect->SetScript(script);
		// clean up event list here?
	}
	else if (effect) // we need a magic script and some var won't be initialized
		return true;

	if (oldScript)
	{
		*refResult = oldScript->refID;
	}

	if ((script->IsQuestScript() && form->typeID == kFormType_TESQuest) || script->IsObjectScript())
	{
		scriptForm->script = script;
		// clean up event list here?
		// This is necessary in order to make sure the script uses the correct questDelayTime.
		script->quest = DYNAMIC_CAST(form, TESForm, TESQuest);
	}
	if (script->IsObjectScript() && !parmForm && thisObj)
	{
		// Re-building ExtraScript entry - the new script then starts running immediately (instead of only on reload).
		thisObj->extraDataList.RemoveByType(kExtraData_Script);
		thisObj->extraDataList.Add(ExtraScript::Create(form, true));

		// Commented out until solved

		//if (thisObj->extraDataList.Add(ExtraScript::Create(form, true))) {
		//	ExtraScript* xScript = (ExtraScript*)thisObj->extraDataList.GetByType(kExtraData_Script);
		//	DoCheckScriptRunnerAndRun(thisObj, &(thisObj->extraDataList));
		//	//MarkBaseExtraListScriptEvent(thisObj, &(thisObj->extraDataList), ScriptEventList::kEvent_OnLoad);
		//	if (xScript) {
		//		xScript->EventCreate(ScriptEventList::kEvent_OnLoad, NULL);
		//	}
		//}
	}
	return true;
}

bool Cmd_IsFormValid_Execute(COMMAND_ARGS)
{
	TESForm *pForm = nullptr;
	*result = 0;
	if (ExtractArgsEx(EXTRACT_ARGS_EX, &pForm))
	{
		pForm = pForm->TryGetREFRParent();
		if (!pForm)
		{
			if (!thisObj)
				return true;
			pForm = thisObj->baseForm;
		}
		if (pForm)
		{
			*result = 1;
		}
		if (IsConsoleMode())
			Console_Print(*result == 1.0 ? "Valid Form!" : "Invalid Form");
	}
	return true;
}

bool Cmd_IsReference_Execute(COMMAND_ARGS)
{
	TESObjectREFR *refr = nullptr;
	*result = 0;
	if (ExtractArgs(EXTRACT_ARGS, &refr))
		*result = 1;
	if (IsConsoleMode())
		Console_Print(*result == 1.0 ? "IsReference" : "Not reference!");

	return true;
}

enum
{
	eScriptVar_Get = 1,
	eScriptVar_GetRef,
	eScriptVar_Has,
};

bool GetVariable_Execute(COMMAND_ARGS, UInt32 whichAction)
{
	char varName[256];
	TESQuest *quest = nullptr;
	Script *targetScript = nullptr;
	ScriptEventList *targetEventList = nullptr;
	*result = 0;

	if (!ExtractArgs(EXTRACT_ARGS, &varName, &quest))
		return true;
	if (quest)
	{
		const auto scriptable = DYNAMIC_CAST(quest, TESQuest, TESScriptableForm);
		targetScript = scriptable->script;
		targetEventList = quest->scriptEventList;
	}
	else if (thisObj)
	{
		const auto scriptable = DYNAMIC_CAST(thisObj->baseForm, TESForm, TESScriptableForm);
		if (scriptable)
		{
			targetScript = scriptable->script;
			targetEventList = thisObj->GetEventList();
		}
	}

	if (targetScript && targetEventList)
	{
		VariableInfo *varInfo = targetScript->GetVariableByName(varName);
		if (varInfo)
		{
			if (whichAction == eScriptVar_Has)
				*result = 1;
			else
			{
				ScriptLocal *var = targetEventList->GetVariable(varInfo->idx);
				if (var)
				{
					if (whichAction == eScriptVar_Get)
						*result = var->data;
					else if (whichAction == eScriptVar_GetRef)
					{
						const auto refResult = (UInt32 *)result;
						*refResult = (*(UInt64 *)&var->data);
					}
				}
			}
		}
	}

	return true;
}

bool Cmd_SetVariable_Execute(COMMAND_ARGS)
{
	char varName[256];
	TESQuest *quest = nullptr;
	Script *targetScript = nullptr;
	ScriptEventList *targetEventList = nullptr;
	float value = 0;
	*result = 0;

	if (!ExtractArgs(EXTRACT_ARGS, &varName, &value, &quest))
		return true;
	if (quest)
	{
		const auto scriptable = DYNAMIC_CAST(quest, TESQuest, TESScriptableForm);
		targetScript = scriptable->script;
		targetEventList = quest->scriptEventList;
	}
	else if (thisObj)
	{
		const auto scriptable = DYNAMIC_CAST(thisObj->baseForm, TESForm, TESScriptableForm);
		if (scriptable)
		{
			targetScript = scriptable->script;
			targetEventList = thisObj->GetEventList();
		}
	}

	if (targetScript && targetEventList)
	{
		VariableInfo *varInfo = targetScript->GetVariableByName(varName);
		if (varInfo)
		{
			ScriptLocal *var = targetEventList->GetVariable(varInfo->idx);
			if (var)
				var->data = value;
		}
	}

	return true;
}

bool Cmd_SetRefVariable_Execute(COMMAND_ARGS)
{
	char varName[256];
	TESQuest *quest = nullptr;
	Script *targetScript = nullptr;
	ScriptEventList *targetEventList = nullptr;
	TESForm *value = nullptr;
	*result = 0;

	if (!ExtractArgs(EXTRACT_ARGS, &varName, &value, &quest))
		return true;
	if (quest)
	{
		const auto scriptable = DYNAMIC_CAST(quest, TESQuest, TESScriptableForm);
		targetScript = scriptable->script;
		targetEventList = quest->scriptEventList;
	}
	else if (thisObj)
	{
		const auto scriptable = DYNAMIC_CAST(thisObj->baseForm, TESForm, TESScriptableForm);
		if (scriptable)
		{
			targetScript = scriptable->script;
			targetEventList = thisObj->GetEventList();
		}
	}

	if (targetScript && targetEventList)
	{
		VariableInfo *varInfo = targetScript->GetVariableByName(varName);
		if (varInfo)
		{
			ScriptLocal *var = targetEventList->GetVariable(varInfo->idx);
			if (var)
			{
				auto refResult = (UInt32 *)result;
				(*(UInt64 *)&var->data) = value ? value->refID : 0;
			}
		}
	}

	return true;
}

bool Cmd_HasVariable_Execute(COMMAND_ARGS)
{
	GetVariable_Execute(PASS_COMMAND_ARGS, eScriptVar_Has);
	return true;
}

bool Cmd_GetVariable_Execute(COMMAND_ARGS)
{
	GetVariable_Execute(PASS_COMMAND_ARGS, eScriptVar_Get);
	return true;
}

bool Cmd_GetRefVariable_Execute(COMMAND_ARGS)
{
	GetVariable_Execute(PASS_COMMAND_ARGS, eScriptVar_GetRef);
	return true;
}

bool Cmd_GetArrayVariable_Execute(COMMAND_ARGS)
{
	if (!ExpressionEvaluator::Active())
	{
		ShowRuntimeError(scriptObj, "GetArrayVariable must be called within the context of an NVSE expression");
		return false;
	}

	GetVariable_Execute(PASS_COMMAND_ARGS, eScriptVar_Get);
	return true;
}

bool Cmd_CompareScripts_Execute(COMMAND_ARGS)
{
	Script *script1 = nullptr;
	Script *script2 = nullptr;
	*result = 0;

	if (!ExtractArgsEx(EXTRACT_ARGS_EX, &script1, &script2))
		return true;
	script1 = DYNAMIC_CAST(script1, TESForm, Script);
	script2 = DYNAMIC_CAST(script2, TESForm, Script);

	if (script1 && script2 && script1->info.dataLength == script2->info.dataLength)
	{
		if (script1 == script2)
			*result = 1;
		else if (!memcmp(script1->data, script2->data, script1->info.dataLength))
			*result = 1;
	}

	return true;
}

bool Cmd_ResetAllVariables_Execute(COMMAND_ARGS)
{
	//sets all vars to 0
	*result = 0;

	ScriptEventList *list = eventList; //reset calling script by default
	if (thisObj)					   //call on a reference to reset that ref's script vars
		list = thisObj->GetEventList();

	if (list)
		*result = list->ResetAllVariables();

	return true;
}

class ExplicitRefFinder
{
public:
	bool Accept(const Script::RefVariable *var)
	{
		if (var && var->varIdx == 0)
			return true;

		return false;
	}
};

Script *GetScriptArg(TESObjectREFR *thisObj, TESForm *form)
{
	Script *targetScript = nullptr;
	if (form)
		targetScript = DYNAMIC_CAST(form, TESForm, Script);
	else if (thisObj)
	{
		if (const auto scriptable = DYNAMIC_CAST(thisObj->baseForm, TESForm, TESScriptableForm))
			targetScript = scriptable->script;
	}

	return targetScript;
}

bool Cmd_GetNumExplicitRefs_Execute(COMMAND_ARGS)
{
	TESForm *form = nullptr;
	Script *targetScript = nullptr;
	*result = 0;

	if (ExtractArgsEx(EXTRACT_ARGS_EX, &form))
	{
		targetScript = GetScriptArg(thisObj, form);
		if (targetScript)
			*result = targetScript->refList.CountIf(ExplicitRefFinder());
	}

	if (IsConsoleMode())
		Console_Print("GetNumExplicitRefs >> %.0f", *result);

	return true;
}

bool Cmd_GetNthExplicitRef_Execute(COMMAND_ARGS)
{
	TESForm *form = nullptr;
	UInt32 refIdx = 0;
	const auto refResult = reinterpret_cast<UInt32*>(result);
	*refResult = 0;

	if (ExtractArgsEx(EXTRACT_ARGS_EX, &refIdx, &form))
	{
		if (const Script *targetScript = GetScriptArg(thisObj, form))
		{
			UInt32 count = 0;
			const Script::RefVariable *entry = nullptr;
			while (count <= refIdx)
			{
				entry = targetScript->refList.Find(ExplicitRefFinder());
				if (!entry)
					break;

				count++;
			}

			if (count == refIdx + 1 && entry && entry->form)
			{
				*refResult = entry->form->refID;
				if (IsConsoleMode())
					Console_Print("GetNthExplicitRef >> %s (%08x)", GetFullName(entry->form), *refResult);
			}
		}
	}

	return true;
}

bool Cmd_RunScript_Execute(COMMAND_ARGS)
{
	TESForm *form = nullptr;

	if (ExtractArgsEx(EXTRACT_ARGS_EX, &form))
	{

		form = form->TryGetREFRParent();
		if (!form)
		{
			if (!thisObj)
				return true;
			form = thisObj->baseForm;
		}

		const auto scriptForm = DYNAMIC_CAST(form, TESForm, TESScriptableForm);
		Script *script = nullptr;
		EffectSetting *effect = nullptr;
		if (!scriptForm) // Let's try for a MGEF
		{
			effect = DYNAMIC_CAST(form, TESForm, EffectSetting);
			if (effect)
				script = effect->GetScript();
			else
			{
				script = DYNAMIC_CAST(form, TESForm, Script);
			}
		}
		else
			script = scriptForm->script;

		if (script)
		{
			const bool runResult = CALL_MEMBER_FN(script, Execute)(thisObj, nullptr, nullptr, 0);
			Console_Print("ran script, returned %s", runResult ? "true" : "false");
		}
	}

	return true;
}

bool Cmd_GetCurrentScript_Execute(COMMAND_ARGS)
{
	// apparently this is useful
	const auto refResult = (UInt32 *)result;
	*refResult = scriptObj->refID;
	return true;
}

bool Cmd_GetCallingScript_Execute(COMMAND_ARGS)
{
	const auto refResult = (UInt32 *)result;
	*refResult = 0;
	Script *caller = UserFunctionManager::GetInvokingScript(scriptObj);
	if (caller)
	{
		*refResult = caller->refID;
	}

	return true;
}

static constexpr auto maxEventNameLen = 0x20;

bool ExtractEventCallback(ExpressionEvaluator &eval, EventManager::EventCallback &outCallback, char *outName, bool addEvt)
{
	if (eval.ExtractArgs() && eval.NumArgs() >= 2)
	{
		const char *eventName = eval.Arg(0)->GetString();
		auto script = DYNAMIC_CAST(eval.Arg(1)->GetTESForm(), TESForm, Script);
		if (eventName && script)
		{
			outCallback.toCall = script;
			strcpy_s(outName, maxEventNameLen, eventName);

			const char* funcName = addEvt ? "SetEventHandler" : "RemoveEventHandler";

			// any filters?
			for (auto i = 2; i < eval.NumArgs(); i++)
			{
				const TokenPair* pair = eval.Arg(i)->GetPair();
				if (pair && pair->left && pair->right) [[likely]]
				{
					const char* key = pair->left->GetString();
					if (key && StrLen(key))
					{
						if (!StrCompare(key, "ref") || !StrCompare(key, "first"))
						{
							outCallback.source = pair->right->GetTESForm();
						}
						else if (!StrCompare(key, "object") || !StrCompare(key, "second"))
						{
							outCallback.object = pair->right->GetTESForm();
						}
					}
					// new system, above preserved for backwards compatibility
					else
					{
						const auto index = static_cast<int>(pair->left->GetNumber());
						if (index < 0) [[unlikely]]
						{
							eval.Error("Invalid index %d passed to %s (arg indices start from 1, and callingReference is filter #0).", funcName);
							return false;
						}

						const auto basicToken = pair->right->ToBasicToken();
						SelfOwningArrayElement element;
						if (basicToken && BasicTokenToElem(basicToken.get(), element)) [[likely]]
						{
							if (const auto [it, success] = outCallback.filters.emplace(index, std::move(element));
								!success) [[unlikely]]
							{
								eval.Error("Event filter index %u appears more than once in %s call.", index, funcName);
							}
						}
					}
				}
			}
			return true;
		}
	}

	return false;
}

bool ProcessEventHandler(char *eventName, EventManager::EventCallback &callback, bool addEvt, ExpressionEvaluator &eval)
{
	if (GetLNEventMask)
	{
		char *colon = strchr(eventName, ':');
		bool separatedStr = false;
		if (colon)
		{
			*(colon++) = 0;
			separatedStr = true;
		}
		if (const UInt32 eventMask = GetLNEventMask(eventName))
		{
			UInt32 const numFilter = (colon && *colon) ? atoi(colon) : 0;

			TESForm* formFilter = callback.source;
			if (!formFilter)
			{
				// Support for using 1::SomeFilter instead of "source"::SomeFilter.
				auto const iter = callback.filters.find(1);
				UInt32 outRefID;
				if (iter->second.GetAsFormID(&outRefID))
					formFilter = LookupFormByID(outRefID);
			}

			return ProcessLNEventHandler(eventMask, callback.TryGetScript(), addEvt, formFilter, numFilter);
		}
		if (separatedStr)
		{
			//restore string back to how it was.
			--colon;
			*colon = ':';
		}
	}
	return addEvt ? EventManager::SetHandler(eventName, callback, &eval)
		: EventManager::RemoveHandler(eventName, callback);
}

bool Cmd_SetEventHandler_Execute(COMMAND_ARGS)
{
	ExpressionEvaluator eval(PASS_COMMAND_ARGS);
	EventManager::EventCallback callback;
	char eventName[maxEventNameLen];
	*result = (ExtractEventCallback(eval, callback, eventName, true)
		&& ProcessEventHandler(eventName, callback, true, eval));
	return true;
}

bool Cmd_RemoveEventHandler_Execute(COMMAND_ARGS)
{
	ExpressionEvaluator eval(PASS_COMMAND_ARGS);
	EventManager::EventCallback callback;
	char eventName[maxEventNameLen];
	*result = (ExtractEventCallback(eval, callback, eventName, false)
		&& ProcessEventHandler(eventName, callback, false, eval));
	return true;
}

bool Cmd_GetCurrentEventName_Execute(COMMAND_ARGS)
{
	AssignToStringVar(PASS_COMMAND_ARGS, EventManager::GetCurrentEventName());
	return true;
}

bool Cmd_DispatchEvent_Execute(COMMAND_ARGS)
{
	ExpressionEvaluator eval(PASS_COMMAND_ARGS);
	*result = 0;
	if (!eval.ExtractArgs() || eval.NumArgs() == 0)
		return true;

	const char *eventName = eval.Arg(0)->GetString();
	if (!eventName) [[unlikely]]
		return true;

	ArrayID argsArrayId = 0;
	const char *senderName = nullptr;
	if (eval.NumArgs() > 1)
	{
		if (!eval.Arg(1)->CanConvertTo(kTokenType_Array))
			return true;
		argsArrayId = eval.Arg(1)->GetArrayID();

		if (eval.NumArgs() > 2)
			senderName = eval.Arg(2)->GetString();
	}

	*result = EventManager::DispatchUserDefinedEvent(eventName, scriptObj, argsArrayId, senderName);
	return true;
}

bool Cmd_DispatchEventAlt_Execute(COMMAND_ARGS)
{
	ExpressionEvaluator eval(PASS_COMMAND_ARGS);
	*result = 0;
	if (!eval.ExtractArgs() || eval.NumArgs() == 0) [[unlikely]]
		return true;

	const char* eventName = eval.Arg(0)->GetString();
	if (!eventName) [[unlikely]]
		return true;

	// does an EventInfo entry already exist for this event?
	const UInt32 eventID = EventManager::EventIDForString(eventName);
	if (EventManager::kEventID_INVALID == eventID)
	{
		*result = 1;	// assume the event may not have any handlers Set.
		// Sucks we can't warn users about having a potentially invalid eventName, though.
		return true;
	}

	auto& eventInfo = EventManager::s_eventInfos[eventID];
#if _DEBUG
	if (!eventInfo.IsUserDefined() && eventID != EventManager::kEventID_DebugEvent) [[unlikely]]
	{
		return true;
	}
#else
	if (!eventInfo.IsUserDefined()) [[unlikely]]
	{
		return true;
	}
#endif

	EventManager::ArgStack params;
	auto const numArgs = eval.NumArgs();
	for (size_t i = 1; i < numArgs; i++)
	{
		auto const arg = eval.Arg(i)->GetAsVoidArg();
		params->push_back(arg);
	}

	*result = EventManager::DispatchEventRaw<true>(thisObj, eventInfo, params);
	return true;
}

bool Cmd_DumpEventHandlers_Execute(COMMAND_ARGS)
{
	ExpressionEvaluator eval(PASS_COMMAND_ARGS);
	if (!eval.ExtractArgs())
		return true;
	auto const numArgs = eval.NumArgs();

	UInt32 eventID = EventManager::kEventID_INVALID;
	Script* script = nullptr;
	if (numArgs >= 1)
	{
		if (const char* eventName = eval.Arg(0)->GetString();
			eventName && eventName[0])
		{
			eventID = EventManager::EventIDForString(eventName);
		}

		if (numArgs >= 2)
		{
			script = eval.Arg(1)->GetUserFunction();
		}
	}

	EventManager::ArgStack argsToFilter{};
	for (size_t i = 2; i < numArgs; i++)
	{
		auto const arg = eval.Arg(i)->GetAsVoidArg();
		argsToFilter->push_back(arg);
	}

	Console_Print("DumpEventHandlers >> Beginning dump.");

	// Dumps all (matching) callbacks of the EventInfo
	auto const DumpEventInfo = [&argsToFilter, script, thisObj](const EventManager::EventInfo &info)
	{
		Console_Print("== Dumping for event %s ==", info.evName);

		if (script)
		{
			auto const range = info.callbacks.equal_range(script);
			for (auto i = range.first; i != range.second; ++i)
			{
				auto const& eventCallback = i->second;
				if (!eventCallback.IsRemoved() && (argsToFilter->empty() ||
					EventManager::DoFiltersMatch<true>(thisObj, info, eventCallback, argsToFilter)))
				{
					std::string toPrint = FormatString(">> Handler: %s, filters: %s", eventCallback.GetCallbackFuncAsStr().c_str(),
						eventCallback.GetFiltersAsStr().c_str());
					Console_Print(toPrint);
				}
			}
		}
		else
		{
			for (auto const &[key, eventCallback] : info.callbacks)
			{
				if (!eventCallback.IsRemoved() && (argsToFilter->empty()
					|| EventManager::DoFiltersMatch<true>(thisObj, info, eventCallback, argsToFilter)))
				{
					std::string toPrint = FormatString(">> Handler: %s, filters: %s", eventCallback.GetCallbackFuncAsStr().c_str(),
						eventCallback.GetFiltersAsStr().c_str());
					Console_Print(toPrint);
				}
			}
		}
	};

	if (eventID == EventManager::kEventID_INVALID)
	{
		// loop through all eventInfo callbacks, filtering by script + filters
		for (auto eventInfoIter = EventManager::s_eventInfos.Begin();
			!eventInfoIter.End(); ++eventInfoIter)
		{
			auto const& eventInfo = eventInfoIter.Get();
			DumpEventInfo(eventInfo);
		}
	}
	else //filtered by eventID
	{
		auto const& eventInfo = EventManager::s_eventInfos[eventID];
		DumpEventInfo(eventInfo);
	}

	return true;
}


bool Cmd_GetEventHandlers_Execute(COMMAND_ARGS)
{
	ExpressionEvaluator eval(PASS_COMMAND_ARGS);
	*result = 0;
	if (!eval.ExtractArgs())
		return true;
	auto const numArgs = eval.NumArgs();

	UInt32 eventID = EventManager::kEventID_INVALID;
	Script* script = nullptr;
	if (numArgs >= 1)
	{
		if (const char* eventName = eval.Arg(0)->GetString();
			eventName && eventName[0])
		{
			eventID = EventManager::EventIDForString(eventName);
			if (eventID == EventManager::kEventID_INVALID)
				return true; //trying to filter by invalid eventName
		}

		if (numArgs >= 2)
		{
			script = eval.Arg(1)->GetUserFunction();
		}
	}

	EventManager::ArgStack argsToFilter{};
	for (size_t i = 2; i < numArgs; i++)
	{
		auto const arg = eval.Arg(i)->GetAsVoidArg(); //numeric args will always be packed as floats
		argsToFilter->push_back(arg);
	}

	// Dumps all (matching) callbacks of the EventInfo
	auto const GetEventInfoHandlers = [=, &argsToFilter](const EventManager::EventInfo& info) -> ArrayVar*
	{
		ArrayVar* handlersForEventArray = g_ArrayMap.Create(kDataType_Numeric, true, scriptObj->GetModIndex());

		auto constexpr GetHandlerArr = [](const EventManager::EventCallback& callback, const Script* scriptObj) -> ArrayVar*
		{
			// [0] = callbackFunc (script for udf, or int for func ptr), [1] = filters string map.
			ArrayVar* handlerArr = g_ArrayMap.Create(kDataType_Numeric, true, scriptObj->GetModIndex());

			std::visit(overloaded
				{
					[=](const LambdaManager::Maybe_Lambda& maybeLambda)
					{
						handlerArr->SetElementFormID(0.0, maybeLambda.Get()->refID);
					},
					[=](const EventManager::EventHandler& handler)
					{
						handlerArr->SetElementNumber(0.0, reinterpret_cast<UInt32>(handler));
					}
				}, callback.toCall);

			handlerArr->SetElementArray(1.0, callback.GetFiltersAsArray(scriptObj)->ID());
			return handlerArr;
		};

		if (script)
		{
			auto const range = info.callbacks.equal_range(script);
			double key = 0;
			for (auto i = range.first; i != range.second; ++i)
			{
				auto const& eventCallback = i->second;
				if (!eventCallback.IsRemoved() && (argsToFilter->empty() ||
					EventManager::DoFiltersMatch<true>(thisObj, info, eventCallback, argsToFilter)))
				{
					handlersForEventArray->SetElementArray(key, GetHandlerArr(eventCallback, scriptObj)->ID());
					key++;
				}
			}
		}
		else // no script filter
		{
			double key = 0;
			for (auto const& [callbackFuncKey, eventCallback] : info.callbacks)
			{
				if (!eventCallback.IsRemoved() && (argsToFilter->empty()
					|| EventManager::DoFiltersMatch<true>(thisObj, info, eventCallback, argsToFilter)))
				{
					handlersForEventArray->SetElementArray(key, GetHandlerArr(eventCallback, scriptObj)->ID());
					key++;
				}
			}
		}

		return handlersForEventArray;
	};

	if (eventID == EventManager::kEventID_INVALID)
	{
		// keys = event handler names, values = an array containing arrays that have [0] = callbackFunc, [1] = filters string map.
		ArrayVar* eventsMap = g_ArrayMap.Create(kDataType_String, false, scriptObj->GetModIndex());
		*result = eventsMap->ID();

		// loop through all eventInfo callbacks, filtering by script + filters
		for (auto eventInfoIter = EventManager::s_eventInfos.Begin();
			!eventInfoIter.End(); ++eventInfoIter)
		{
			auto const& eventInfo = eventInfoIter.Get();
			eventsMap->SetElementArray(eventInfo.evName, GetEventInfoHandlers(eventInfo)->ID());
		}
	}
	else //filtered by eventID
	{
		auto const& eventInfo = EventManager::s_eventInfos[eventID];
		// return an array containing arrays that have [0] = callbackFunc, [1] = filters string map.
		*result = GetEventInfoHandlers(eventInfo)->ID();
	}

	return true;
}

extern float g_gameSecondsPassed;

bool ExtractCallAfterInfo(ExpressionEvaluator& eval, std::list<DelayedCallInfo>& infos, ICriticalSection& cs)
{
	auto const seconds = static_cast<float>(eval.Arg(0)->GetNumber());
	Script* const callFunction = eval.Arg(1)->GetUserFunction();
	if (!callFunction)
		return false;

	//Optional args
	DelayedCallInfo::eFlags flags = DelayedCallInfo::kFlags_None;
	CallArgs args{};

	auto const numArgs = eval.NumArgs();
	if (numArgs > 2)
	{
		flags = static_cast<DelayedCallInfo::eFlags>(eval.Arg(2)->GetNumber());
		args.reserve(numArgs - 3);
		for (UInt32 i = 3; i < numArgs; i++)
		{
			if (auto const tok = eval.Arg(i))
			{
				SelfOwningArrayElement elem;
				BasicTokenToElem(tok, elem);
				args.emplace_back(std::move(elem));
			}
			else [[unlikely]]
				break;
		}
	}

	ScopedLock lock(cs);
	infos.emplace_back(callFunction, g_gameSecondsPassed + seconds, eval.m_thisObj, flags, std::move(args));
	return true;
}

bool ExtractCallAfterInfo_OLD(COMMAND_ARGS, std::list<DelayedCallInfo>& infos, ICriticalSection& cs)
{
	float time;
	Script* callFunction;
	UInt32 runInMenuMode = false;
	if (!ExtractArgs(EXTRACT_ARGS, &time, &callFunction, &runInMenuMode) || !callFunction || !IS_ID(callFunction, Script))
		return false;

	ScopedLock lock(cs);
	infos.emplace_back(callFunction, g_gameSecondsPassed + time, thisObj, runInMenuMode ? DelayedCallInfo::kFlag_RunInMenuMode : DelayedCallInfo::kFlags_None);
	return true;
}

std::list<DelayedCallInfo> g_callAfterInfos;
ICriticalSection g_callAfterInfosCS;

bool Cmd_CallAfterSeconds_Execute(COMMAND_ARGS)
{
	*result = false; //bSuccess
	if (ExpressionEvaluator eval(PASS_COMMAND_ARGS);
		eval.ExtractArgs())
	{
		*result = ExtractCallAfterInfo(eval, g_callAfterInfos, g_callAfterInfosCS);
	}
	return true;
}
bool Cmd_CallAfterSeconds_OLD_Execute(COMMAND_ARGS)
{
	*result = ExtractCallAfterInfo_OLD(PASS_COMMAND_ARGS, g_callAfterInfos, g_callAfterInfosCS);
	return true;
}

std::list<DelayedCallInfo> g_callForInfos;
ICriticalSection g_callForInfosCS;

bool Cmd_CallForSeconds_Execute(COMMAND_ARGS)
{
	*result = false; //bSuccess
	if (ExpressionEvaluator eval(PASS_COMMAND_ARGS);
		eval.ExtractArgs())
	{
		*result = ExtractCallAfterInfo(eval, g_callForInfos, g_callForInfosCS);
	}
	return true;
}
bool Cmd_CallForSeconds_OLD_Execute(COMMAND_ARGS)
{
	*result = ExtractCallAfterInfo_OLD(PASS_COMMAND_ARGS, g_callForInfos, g_callForInfosCS);
	return true;
}

bool ExtractCallWhileInfo(ExpressionEvaluator &eval, std::list<CallWhileInfo> &infos, ICriticalSection &cs)
{
	Script* callFunction = eval.Arg(0)->GetUserFunction();
	Script* conditionFunction = eval.Arg(1)->GetUserFunction();
	if (!callFunction || !conditionFunction)
		return false;

	//Optional args
	CallWhileInfo::eFlags flags = CallWhileInfo::kFlags_None;
	CallArgs args{};

	auto const numArgs = eval.NumArgs();
	if (numArgs > 2)
	{
		flags = static_cast<CallWhileInfo::eFlags>(eval.Arg(2)->GetNumber());
		args.reserve(numArgs - 3);
		for (UInt32 i = 3; i < numArgs; i++)
		{
			if (auto const tok = eval.Arg(i))
			{
				SelfOwningArrayElement elem;
				BasicTokenToElem(tok, elem);
				args.emplace_back(std::move(elem));
			}
			else [[unlikely]]
				break;
		}
	}

	ScopedLock lock(cs);
	infos.emplace_back(callFunction, conditionFunction, eval.m_thisObj, flags, std::move(args));
	return true;
}
bool ExtractCallWhileInfo_OLD(COMMAND_ARGS, std::list<CallWhileInfo>& infos, ICriticalSection& cs)
{
	Script* callFunction;
	Script* conditionFunction;
	if (!ExtractArgs(EXTRACT_ARGS, &callFunction, &conditionFunction))
		return false;

	for (auto* form : { callFunction, conditionFunction })
		if (!form || !IS_ID(form, Script))
			return false;

	ScopedLock lock(cs);
	infos.emplace_back(callFunction, conditionFunction, thisObj, CallWhileInfo::kFlags_None);
	return true;
}

std::list<CallWhileInfo> g_callWhileInfos;
ICriticalSection g_callWhileInfosCS;

bool Cmd_CallWhile_Execute(COMMAND_ARGS)
{
	*result = false; //bSuccess
	if (ExpressionEvaluator eval(PASS_COMMAND_ARGS);
		eval.ExtractArgs())
	{
		*result = ExtractCallWhileInfo(eval, g_callWhileInfos, g_callWhileInfosCS);
	}
	return true;
}
bool Cmd_CallWhile_OLD_Execute(COMMAND_ARGS)
{
	*result = ExtractCallWhileInfo_OLD(PASS_COMMAND_ARGS, g_callWhileInfos, g_callWhileInfosCS);
	return true;
}


std::list<CallWhileInfo> g_callWhenInfos;
ICriticalSection g_callWhenInfosCS;

bool Cmd_CallWhen_Execute(COMMAND_ARGS)
{
	*result = false; //bSuccess
	if (ExpressionEvaluator eval(PASS_COMMAND_ARGS);
		eval.ExtractArgs())
	{
		*result = ExtractCallWhileInfo(eval, g_callWhenInfos, g_callWhenInfosCS);
	}
	return true;
}
bool Cmd_CallWhen_OLD_Execute(COMMAND_ARGS)
{
	*result = ExtractCallWhileInfo_OLD(PASS_COMMAND_ARGS, g_callWhenInfos, g_callWhenInfosCS);
	return true;
}

void DecompileScriptToFolder(const std::string& scriptName, Script* script, const std::string& fileExtension, const std::string_view& modName)
{
	ScriptParsing::ScriptAnalyzer analyzer(script);
	if (analyzer.error)
	{
		if (IsConsoleMode())
			Console_Print("Script %s is not compiled", scriptName.c_str());
		return;
	}
	const auto* dirName = "DecompiledScripts";
	if (!std::filesystem::exists(dirName))
		std::filesystem::create_directory(dirName);
	const auto modDirName = FormatString("%s/%s", dirName, modName.data());
	if (!std::filesystem::exists(modDirName))
		std::filesystem::create_directory(modDirName);
	const auto filePath = modDirName + '/' + scriptName + '.' + fileExtension;
	std::ofstream os(filePath);
	os << analyzer.DecompileScript();
	if (IsConsoleMode())
		Console_Print("Decompiled script to '%s'", filePath.c_str());
}

bool Cmd_DecompileScript_Execute(COMMAND_ARGS)
{
	TESForm* form;
	*result = 0;
	char fileExtensionArg[0x100]{};
	if (!ExtractArgs(EXTRACT_ARGS, &form, &fileExtensionArg))
		return true;
	std::string fileExtension;
	if (fileExtensionArg[0])
		fileExtension = std::string(fileExtensionArg);
	else
		fileExtension = "gek";
	if (IS_ID(form, Script))
	{
		auto* script = static_cast<Script*>(form);
		std::string name = script->GetName();
		if (name.empty())
			name = FormatString("%X", script->refID & 0x00FFFFFF);
		DecompileScriptToFolder(name, script, fileExtension, GetModName(script));
	}
	else if (IS_ID(form, TESPackage))
	{
		auto* package = static_cast<TESPackage*>(form);
		for (auto& packageEvent : {std::make_pair("OnBegin", &package->onBeginAction), std::make_pair("OnEnd", &package->onEndAction), std::make_pair("OnChange", &package->onChangeAction)})
		{
			auto& [name, action] = packageEvent;
			if (action->script)
				DecompileScriptToFolder(std::string(package->GetName()) + name, action->script, fileExtension, GetModName(package));
		}
	}
	else
		return true;
	*result = 1;
	return true;
}

bool Cmd_HasScriptCommand_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32 commandOpcode;
	TESForm* form;
	UInt32 eventOpcode = -1;
	Script* script = nullptr;
	if (!ExtractArgs(EXTRACT_ARGS, &commandOpcode, &form, &eventOpcode))
		return true;
	if (!form)
		form = thisObj;
	if (!form)
		return true;
	if (IS_ID(form, Script))
		script = static_cast<Script*>(form);
	else if (form->GetIsReference())
	{
		const auto* ref = static_cast<TESObjectREFR*>(form);
		if (const auto* extraScript = ref->GetExtraScript())
			script = extraScript->script;
	}
	if (!script)
		return true;
	auto* cmdInfo = g_scriptCommands.GetByOpcode(commandOpcode);
	if (!cmdInfo)
		return true;
	CommandInfo* eventCmd = nullptr;
	if (eventOpcode != -1)
		eventCmd = GetEventCommandInfo(eventOpcode);
	if (ScriptParsing::ScriptContainsCommand(script, cmdInfo, eventCmd))
		*result = 1;
	return true;
}

static MemoizedMap<const char*, UInt32> s_opcodeMap;

bool Cmd_GetCommandOpcode_Execute(COMMAND_ARGS)
{
	*result = 0;
	char buf[0x400];
	if (!ExtractArgs(EXTRACT_ARGS, buf))
		return true;
	*result = s_opcodeMap.Get(buf, [](const char* buf)
	{
		auto* cmd = g_scriptCommands.GetByName(buf);
		if (!cmd)
			return 0u;
		return static_cast<unsigned>(cmd->opcode);
	});
	return true;
}

bool Cmd_Ternary_Execute(COMMAND_ARGS)
{
	*result = 0;
	if (ExpressionEvaluator eval(PASS_COMMAND_ARGS);
		eval.ExtractArgs())
	{
		auto const value = eval.Arg(0)->ToBasicToken();
		if (!value)
			return true;	// should never happen, could cause weird behavior otherwise.

		Script* call_udf = nullptr;
		if (value->GetBool()) {
			call_udf = eval.Arg(1)->GetUserFunction();
		}
		else {
			call_udf = eval.Arg(2)->GetUserFunction();
		}
		if (!call_udf)
			return true;

		InternalFunctionCaller caller(call_udf, thisObj, containingObj);
		caller.SetArgs(0);
		if (auto const tokenValResult = UserFunctionManager::Call(std::move(caller)))
			tokenValResult->AssignResult(eval);
	}
	return true;
}

#endif
