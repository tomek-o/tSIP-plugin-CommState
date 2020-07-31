//---------------------------------------------------------------------------

#define _EXPORTING
#include "..\tSIP\tSIP\phone\Phone.h"
#include "..\tSIP\tSIP\phone\PhoneSettings.h"
#include "..\tSIP\tSIP\phone\PhoneCapabilities.h"
#include "Log.h"
#include "Mutex.h"
#include "ScopedLock.h"
#include <assert.h>
#include <algorithm>	// needed by Utils::in_group
#include "Utils.h"
#include <string>
#include <fstream>
#include <json/json.h>

//---------------------------------------------------------------------------

static const struct S_PHONE_DLL_INTERFACE dll_interface =
{DLL_INTERFACE_MAJOR_VERSION, DLL_INTERFACE_MINOR_VERSION};

// callback ptrs
CALLBACK_LOG lpLogFn = NULL;
CALLBACK_CONNECT lpConnectFn = NULL;
CALLBACK_KEY lpKeyFn = NULL;
CALLBACK_SET_VARIABLE lpSetVariableFn = NULL;
CALLBACK_CLEAR_VARIABLE lpClearVariableFn = NULL;

void *callbackCookie;	///< used by upper class to distinguish library instances when receiving callbacks

#define DEFAULT_CMD "START CMD /C \"ECHO Add command to be executed when ringing to plugin config file (after closing softphone) && PAUSE\""

namespace {
std::string path = Utils::GetDllPath();
std::string dllName = Utils::ExtractFileNameWithoutExtension(path);

std::string configPath = Utils::ReplaceFileExtension(Utils::GetDllPath(), ".cfg");
std::string comPort =  "\\\\.\\COM1";
Mutex mutex;
HANDLE commHandle = INVALID_HANDLE_VALUE;
}


extern "C" __declspec(dllexport) void GetPhoneInterfaceDescription(struct S_PHONE_DLL_INTERFACE* interf) {
    interf->majorVersion = dll_interface.majorVersion;
    interf->minorVersion = dll_interface.minorVersion;
}

void Log(const char* txt) {
    if (lpLogFn)
        lpLogFn(callbackCookie, txt);
}

void Connect(int state, char *szMsgText) {
    if (lpConnectFn)
        lpConnectFn(callbackCookie, state, szMsgText);
}

void Key(int keyCode, int state) {
    if (lpKeyFn)
        lpKeyFn(callbackCookie, keyCode, state);
}

void SetCallbacks(void *cookie, CALLBACK_LOG lpLog, CALLBACK_CONNECT lpConnect, CALLBACK_KEY lpKey) {
    assert(cookie && lpLog && lpConnect && lpKey);
    lpLogFn = lpLog;
    lpConnectFn = lpConnect;
    lpKeyFn = lpKey;
    callbackCookie = cookie;
    lpLogFn(callbackCookie, "CommState dll loaded\n");

    //armScope.callbackLog = Log;
    //armScope.callbackConnect = Connect;
}

void GetPhoneCapabilities(struct S_PHONE_CAPABILITIES **caps) {
    static struct S_PHONE_CAPABILITIES capabilities = {
        0
    };
    *caps = &capabilities;
}

void ShowSettings(HANDLE parent) {
    MessageBox((HWND)parent, "No additional settings.", "Device DLL", MB_ICONINFORMATION);
}

static int WorkerThreadStart(void);
static int WorkerThreadStop(void);

int Connect(void) {
    Log("Connect\n");
    WorkerThreadStart();
    return 0;
}

int Disconnect(void) {
    Log("Disconnect\n");
    WorkerThreadStop();
    return 0;
}

static bool bSettingsReaded = false;

static int GetDefaultSettings(struct S_PHONE_SETTINGS* settings) {

    return 0;
}

int GetPhoneSettings(struct S_PHONE_SETTINGS* settings) {
    assert(settings);
    Json::Value root;   // will contains the root value after parsing.
    Json::Reader reader;

    std::ifstream ifs(configPath.c_str());
    std::string strConfig((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    bool parsingSuccessful = reader.parse( strConfig, root );
    if ( !parsingSuccessful )
        return GetDefaultSettings(settings);

    GetDefaultSettings(settings);

    comPort = root.get("ComPort", comPort.c_str()).asString();

    bSettingsReaded = true;
    return 0;
}

int SavePhoneSettings(struct S_PHONE_SETTINGS* settings) {
    Json::Value root;
    Json::StyledWriter writer;

    root["ComPort"] = comPort.c_str();

    std::string outputConfig = writer.write( root );
    std::ofstream ofs(configPath.c_str());
    ofs << outputConfig;
    ofs.close();

    return 0;
}

int SetRegistrationState(int state) {
    return 0;
}

int SetCallState(int state, const char* display) {
    return 0;
}

int Ring(int state) {
    //MessageBox(NULL, "Ring", "Device DLL", MB_ICONINFORMATION);
    return 0;
}

int SetVariable(const char* name, const char* value) {
	if (lpSetVariableFn) {
		return lpSetVariableFn(callbackCookie, name, value);
	}
	return -1;
}

int ClearVariable(const char* name) {
	if (lpClearVariableFn) {
		return lpClearVariableFn(callbackCookie, name);
	}
	return -1;
}

void SetSetVariableCallback(CALLBACK_SET_VARIABLE lpFn) {
	lpSetVariableFn = lpFn;
}

void __stdcall SetClearVariableCallback(CALLBACK_CLEAR_VARIABLE lpFn) {
	lpClearVariableFn = lpFn;
}

inline bool StartsWith(const char* &a, const char *b)
{
	if(strlen(a) < strlen(b)) {
		return false;
	}
	else {
		int len = strlen(b);
		bool result = !strnicmp(a,b,len); //case insensitive
		if(result)
			a += len;
		return result;
	}
}

int __stdcall SendMessageText(const char* text) {
	//LOG("received message: %s", text);

	if (StartsWith(text, "SET ")) {
        if (commHandle == INVALID_HANDLE_VALUE) {
            LOG("%s: COM is not opened\n", dllName.c_str());
            return -1;
        }
        BOOL status = 0;
        ScopedLock<Mutex> lock(mutex);
		if (StartsWith(text, "RTS ")) {
            int val = atoi(text);
            if (val) {
                status = EscapeCommFunction(commHandle, SETRTS);
                if (!status) {
                    LOG("%s: EscapeCommFunction error\n", dllName.c_str());
                    CloseHandle(commHandle);
                    commHandle = INVALID_HANDLE_VALUE;
                }
            } else {
                status = EscapeCommFunction(commHandle, CLRRTS);
                if (!status) {
                    LOG("%s: EscapeCommFunction error\n", dllName.c_str());
                    CloseHandle(commHandle);
                    commHandle = INVALID_HANDLE_VALUE;
                }
            }
		} else if (StartsWith(text, "DTR ")) {
            int val = atoi(text);
            if (val) {
                status = EscapeCommFunction(commHandle, SETDTR);
                if (!status) {
                    LOG("%s: EscapeCommFunction error\n", dllName.c_str());
                    CloseHandle(commHandle);
                    commHandle = INVALID_HANDLE_VALUE;
                }
            } else {
                status = EscapeCommFunction(commHandle, CLRDTR);
                if (!status) {
                    LOG("%s: EscapeCommFunction error\n", dllName.c_str());
                    CloseHandle(commHandle);
                    commHandle = INVALID_HANDLE_VALUE;
                }
            }
		}
	}
	return -2;
}


bool connected = false;
bool exited = true;

DWORD WINAPI WorkerThreadProc(LPVOID data) {
	//#pragma warn -8091	// incorrectly issued by BDS2006
	std::transform(dllName.begin(), dllName.end(), dllName.begin(), tolower);
    std::string stateVarName = dllName + "State";
    DWORD lastModemStatus = 0;
    while (connected) {
        {
            ScopedLock<Mutex> lock(mutex);
            if (commHandle == INVALID_HANDLE_VALUE) {
                commHandle = CreateFile(comPort.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0);
            }
            if (commHandle != INVALID_HANDLE_VALUE) {
                DWORD dwModemStatus;
                if (!GetCommModemStatus(commHandle, &dwModemStatus)) {
                    // Error in GetCommModemStatus;
                    ClearVariable(stateVarName.c_str());
                } else {
                #if 0
                    BOOL fCTS, fDSR, fRING, fRLSD;
                    fCTS = MS_CTS_ON & dwModemStatus;
                    fDSR = MS_DSR_ON & dwModemStatus;
                    fRING = MS_RING_ON & dwModemStatus;
                    fRLSD = MS_RLSD_ON & dwModemStatus;
                #endif
                    if (dwModemStatus != lastModemStatus) {
                        lastModemStatus = dwModemStatus;
                        LOG("%s: %s state = %u\n", dllName.c_str(), comPort.c_str(), dwModemStatus);
                    }
                    char buf[32];
                    itoa(dwModemStatus, buf, 10);
                    SetVariable(stateVarName.c_str(), buf);
                }
            } else {
                ClearVariable(stateVarName.c_str());
            }
        }
        for (int i=0; i<10; i++) {
            Sleep(100);
            if (connected)
                break;
        }
    }
    if (commHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(commHandle);
        commHandle = INVALID_HANDLE_VALUE;
    }
    exited = true;
    return 0;
}


int WorkerThreadStart(void) {
    DWORD dwtid;
    exited = false;
    connected = true;
    HANDLE Thread = CreateThread(NULL, 0, WorkerThreadProc, /*this*/NULL, 0, &dwtid);
    if (Thread == NULL) {
        Log("Failed to create worker thread.");
        connected = false;
        exited = true;
    } else {
        Log("Worker thread created.\n");
    }
    return 0;
}

int WorkerThreadStop(void) {
    connected = false;
    while (!exited) {
        Sleep(50);
    }
    return 0;
}
