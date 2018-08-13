#include "CAntiSmac.h"

#include "SDK.h"
#include "Util.h"

bool __stdcall Hooked_ProcessGetCvarValue(SVC_GetCvarValue *msg)
{

	return CAntiSmac::getInst()->processGetCvarValue(msg);

	// VMTManager &hook = VMTManager::GetHook(pClientState);
	// bool bRet = hook.GetMethod<bool(__stdcall *)(SVC_GetCvarValue *)>(gOffsets.iProcessGetCvarValue)(msg);

	// return bRet;
}

bool __stdcall Hooked_ProcessSetConVar(NET_SetConVar *msg)
{

	return CAntiSmac::getInst()->processSetConVar(msg);

	// VMTManager &hook = VMTManager::GetHook(pClientState);
	// bool bRet = hook.GetMethod<bool(__stdcall *)(SVC_GetCvarValue *)>(gOffsets.iProcessGetCvarValue)(msg);

	// return bRet;
}

bool __stdcall Hooked_StringCmd(NET_StringCmd *msg) { return CAntiSmac::getInst()->processStringCmd(msg); }

const char *CAntiSmac::name() const { return "ANTI-SMAC"; }

void CAntiSmac::init()
{
	// hook the stuff
	//clientStateHook = new VMTBaseManager();
	//// hook and manually say that there are 28 vfuncs in the table
	//clientStateHook->Init(gInts->ClientState, 0x8);
	//clientStateHook->HookMethod(&Hooked_ProcessGetCvarValue, gOffsets.processGetCvarValue);
	//clientStateHook->HookMethod(&Hooked_ProcessSetConVar, gOffsets.processSetConVar);
	//clientStateHook->Rehook();

	gHookManager.hookMethod(gInts->ClientState.get(), gOffsets.processGetCvarValue, &Hooked_ProcessGetCvarValue, 0x8);
	gHookManager.hookMethod(gInts->ClientState.get(), gOffsets.processSetConVar, &Hooked_ProcessSetConVar, 0x8);

	// open this file for reading
	auto handle = gFileManager.open(getPathForDll(gInts->thisDll) + "..\\config\\CAntiSmac.txt", std::ios::in);

	if( handle == CFileManager::invalidHandle )
		Log::Console( "unable to get CAntiSmac.txt config file" );

	return;

	// tokenize this file when we start
	cvarList = gFileManager.TokenFile(handle, ':');

	//nameVar = gInts->Cvar->FindVar("name");

	//nameVar->m_fnChangeCallback = nullptr;

	return;
}

void CAntiSmac::processCommand(CUserCmd *pUserCmd)
{
	if(nameSpam.getValue() == true)
	{
		// Log::Console("NameSpam enabled");
		std::string randName;
		for(int i = 0; i < 32; i++)
			randName += "\n";

		char *newName = const_cast<char *>(randName.c_str());

		// add one more character at the end so the server accepts it.
		newName[0] = '_';

		gLocalPlayerVars.name = newName;
	}
	else
	{
		gLocalPlayerVars.name = gInts->steam.friends->GetPersonaName();
	}

	return;
}

bool CAntiSmac::processGetCvarValue(SVC_GetCvarValue *msg)
{
	CLC_RespondCvarValue returnMsg;

	returnMsg.m_iCookie		= msg->m_iCookie;
	returnMsg.m_szCvarName  = msg->m_szCvarName;
	returnMsg.m_szCvarValue = "";
	returnMsg.m_eStatusCode = eQueryCvarValueStatus_ValueIntact; // always pretend that we have it

	Log::Console("{CVAR} GetCvarValue wants cvar %s", msg->m_szCvarName);

	char tempValue[256];

	tempValue[0] = '\0';

	bool hasValue = false;

	// check server vars
	for(auto &var : serverConvars)
	{
		if(!strcmp(msg->m_szCvarName, var.name))
		{
			returnMsg.m_szCvarValue = var.value;
			Log::Console("{CVAR}	we got value %s for cvar %s", returnMsg.m_szCvarValue, msg->m_szCvarName);
			hasValue = true;
			break;
		}
	}

	// possible optimization - doesnt convert every time
	std::string cvarName = msg->m_szCvarName;

	// check custom list
	for(auto &var : cvarList)
	{
		if(var.first == cvarName)
		{
			returnMsg.m_szCvarValue = var.second.c_str();
			Log::Console("{CVAR}	returning custom %s for cvar for %s", returnMsg.m_szCvarValue, msg->m_szCvarName);
			hasValue = true;
			break;
		}
	}

	// see if we got a value for it
	if(!hasValue)
	{
		// if not...
		Log::Console("{CVAR}	unable to return a value for %s: getting it!!", msg->m_szCvarName);

		// Does any ConCommand exist with this name?
		auto pVar = gInts->Cvar->FindVar(msg->m_szCvarName);
		if(pVar)
		{
			if(pVar->m_nFlags & (int)ConvarFlags::FCVAR_SERVER_CANNOT_QUERY)
			{
				// The server isn't allowed to query this.
				// just say its intact anyway
				returnMsg.m_eStatusCode = eQueryCvarValueStatus_ValueIntact;
			}
			else
			{
				returnMsg.m_eStatusCode = eQueryCvarValueStatus_ValueIntact;

				// default values should be allowed... I hope
				Log::Console("{CVAR}	getting default value for cvar %s", msg->m_szCvarName);
				strcpy_s(tempValue, pVar->m_pszDefaultValue);
				returnMsg.m_szCvarValue = tempValue;
			}
		}
		else
		{
			Log::Console("{CVAR}	cvar %s not found!!", msg->m_szCvarName);
			returnMsg.m_eStatusCode = eQueryCvarValueStatus_CvarNotFound;
			returnMsg.m_szCvarValue = tempValue;
		}

		// special constants because fuck you thats why
		if(!strcmp(msg->m_szCvarName, "mat_dxlevel") )
		{
			// mat_dxlevel default value is 0 for some fucking reason
			returnMsg.m_szCvarValue = "95";
		}
	}

	Log::Console("{CVAR} ending getCvarValue for %s by returning '%s'", returnMsg.m_szCvarName, returnMsg.m_szCvarValue);

	// send back via clientstate netchannel
	gInts->Engine->GetNetChannelInfo()->SendNetMsg(returnMsg);

	return true;
}

bool CAntiSmac::processSetConVar(NET_SetConVar *msg)
{
	#if _MSC_VER == 1900
	// Never process on local client, since the ConVar is directly linked here
	if(gInts->Engine->GetNetChannelInfo()->IsLoopback())
		return true;

	bool changed = false;
	
	// TODO: we do need to change some of these convars that handle gamemodes

	for(auto &var : msg->m_ConVars)
	{
		Log::Console("ProcessSetConvar got '%s' with value '%s'", var.name, var.value);
		for(auto &vvar : serverConvars)
		{
			if(!strcmp(var.name, vvar.name))
			{
				changed = true;
				Log::Console("changing value from %s to %s", vvar.value, var.value);
				vvar = var;
			}
		}

		if(!changed)
			serverConvars.push_back(var);
	}

	#endif

	return true;
}

bool CAntiSmac::processStringCmd(NET_StringCmd *msg)
{
	Log::Console("'%s': '%s'", msg->GetName(), msg->m_szCommand);
	return gInts->ClientState->ProcessStringCmd(msg);
}

void CAntiSmac::menuUpdate( F1_IConVar ** menuArray, int & currIndex )
{
	menuArray[ currIndex++ ] = &antiSmacSwitch;

	if(antiSmacSwitch.getValue() )
	{
		menuArray[currIndex++] = &debug;
		menuArray[ currIndex++ ] = &nameSpam;
	}
}
