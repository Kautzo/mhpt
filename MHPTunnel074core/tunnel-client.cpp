/**
 * $Id: tunnel-client.cpp,v 1.33 2008/06/08 09:53:16 pensil Exp $
 * Copyright (c) 2008 Pensil - www.pensil.jp
 * 
 * Tunnel client for MHP
 */
#define _BITTYPES_H
#define WIN32

//#include "tunnel-server.h"

#define DLLAPI extern "C" __declspec(dllexport)

#include "tunnel-client.h"
#include "MakeMD5.h"

/*
 * Desired design of maximum size and alignment.
 * These are implementation specific.
 */
#include "pktOidRequest.h"	// Operation support request OID

#include <time.h>

#include <iostream>
#include <string.h>
#include <conio.h>

#include "tunnel-common.h"

//using namespace std;


// kBufferSize must be larger than the length of kpcEchoMessage.
const int kBufferSize = 4096;
#define maxiMacAddr 200

// Time to determine if it was out of the meeting place
int roomWait=10000;

// Whether you perform an automatic search for SSID
bool ssidAutoSence = false;

// Search interval for automatic search of SSID
int ssidAutoSenceInterval = 10000;

// Whether you perform an automatic search for PSP
bool pspAutoSence = false;

// Search interval for automatic search of PSP
int pspAutoSenceInterval = 4000;

// Value judgment of whether to detect link up
int LinkUpDBM = -60;

NDIS_802_11_RSSI Rssi = -200;

int clientStatus = 0;

int adapterStatus = 0;
HANDLE packetCaptureHandle;
HANDLE ssidMonitorHandle;

CRITICAL_SECTION pcapSection;
CRITICAL_SECTION commandSection;

// BSSID scan list limit of
#define	MAX_BSSID				20

pcap_t *adhandle;
char errbuf[PCAP_ERRBUF_SIZE];

// INI settings
TCHAR szServer[256];
TCHAR szPort[10];
TCHAR szDevice[256];
TCHAR szNickName[30];

//struct USER_INFO {
//	clock_t last;
//	TCHAR szNickName[30];
//};

// A list of MAC addresses to tunnel to the server from their
MAC_ADDRESS mac[maxMacAddr];

// (To prevent loops) a list of MAC addresses that do not tunnel
MAC_ADDRESS ignoreMac[maxiMacAddr];

// The connection to the server
SOCKET_EX gsd;

u_long nRemoteAddress;
int nPort;

bool needReconnect = false;

clock_t last_psp_packet = 0;
clock_t lastDummyPacket = 0;

// Handle of the selected interface
LPADAPTER	m_lpAdapter;

// SSID of the selected
NDIS_802_11_SSID Ssid;

// Meeting place of the currently selected
char room[30];

// BSSID list
NDIS_WLAN_BSSID	m_Bssid[MAX_BSSID];

void packet_handler(u_char *, const struct pcap_pkthdr *header, const u_char *pkt_data);
void doClientConnect(SOCKET_EX * sdex);
void doClientCommand(SOCKET_EX * sdex, const DATA_HEADER * dh, const char * data);
void doClientClose(SOCKET_EX * sdex);
WIRELESS_LAN_DEVICE * FindDevices();

char pb[4096];

const int maxUsers = 100;
USER_INFO users[maxUsers];
bool user_active[maxUsers];

struct COMMAND_RESULT;
struct CONSOLE_LOG_CHAIN;

struct CONSOLE_LOG_CHAIN
{
	CONSOLE_LOG_CHAIN * next;
	CONSOLE_LOG log;
};

//COMMAND_RESULT * command_result;
CONSOLE_LOG_CHAIN * console_log;

STARTUPINFO sinfo;
PROCESS_INFORMATION pinfo;

CRITICAL_SECTION clientLogSection;

void AddConsoleLog(int type, int option, const char * text);

void DummyConsoleLog(int , int , const char * )
{
}

tunnel_handler theCallUI = DummyConsoleLog;

void callUI(int eventType, int option, const char * logText)
{
	switch (eventType)
	{
		case TUNNEL_EVENT_ERROR:
			LogOut("err", logText);
			break;
			// 
		case TUNNEL_EVENT_NOTICE:
			LogOut("ntc", logText);
			break;
			// 
		case TUNNEL_EVENT_CLIENTLOG:
			LogOut("cli", logText);
			break;
			// 
		case TUNNEL_EVENT_SERVERLOG:
			break;
			// 
		case TUNNEL_EVENT_ENTERUSER:
			LogOut("eus", logText);
			break;
			// 
		case TUNNEL_EVENT_LEAVEUSER:
			LogOut("lus", logText);
			break;
			// 
		case TUNNEL_EVENT_ENTERPSP:
			LogOut("eps", logText);
			break;
			// 
		case TUNNEL_EVENT_LEAVEPSP:
			LogOut("lps", logText);
			break;
			// 
		default:
			break;
			// 
	}
	theCallUI(eventType, option, logText);
}

void AddCommandResult(COMMAND_RESULT * * lpResult, const char * text)
{
	if (text == NULL) {
		return;
	}
	char tmp[6];
	GetSetting("debug", "false", tmp, sizeof(tmp));
	if (strcmp(tmp, "true")==0) {
		LogOut("res", text);
	}

	COMMAND_RESULT * result;
	result = (COMMAND_RESULT *)malloc(sizeof(COMMAND_RESULT));
	result->text = (char *)malloc(strlen(text) + 1);
	result->next = NULL;
	strcpy(result->text, text);

	if (*lpResult == NULL) {
		*lpResult = result;
		return;
	}

	COMMAND_RESULT * last_result = *lpResult;

	while (last_result->next != NULL) {
		last_result = last_result->next;
	} 
	last_result->next = result;
}

const char * _stdcall GetCommandResult(COMMAND_RESULT * * lpResult)
{
	COMMAND_RESULT * result = *lpResult;
	if (result == NULL) {
		return NULL;
	}
	free(result->text);
	*lpResult = result->next;
	free(result);
	if (result == NULL) {
		return NULL;
	}
	return result->text;
}

void _stdcall FreeCommandResult(COMMAND_RESULT * result)
{
	while (result != NULL) {
		free(result->text);
		COMMAND_RESULT * next = result->next;
		free(result);
		result = next;
	}
}

bool useEvent;
HANDLE hConsoleLogEvent;

void _stdcall UseConsoleEvent(bool value)
{
	useEvent = value;
}

void AddConsoleLog(int type, int option, const char * text)
{
	if (text == NULL) {
		return;
	}
	EnterCriticalSection(&clientLogSection);

	CONSOLE_LOG_CHAIN * result;

	result = (CONSOLE_LOG_CHAIN *)malloc(sizeof(CONSOLE_LOG_CHAIN));

	result->next = NULL;
	
	result->log.text = (char *)malloc(strlen(text) + 1);
	result->log.type = type;
	result->log.option = option;

//	LogOut("275", text);
	strcpy(result->log.text, text);
	
	if (console_log == NULL) {
		console_log = result;
	} else {
		CONSOLE_LOG_CHAIN * prev = console_log;
		while (prev->next != NULL) {
			prev = prev->next;
		}
		prev->next = result;
	}
	LeaveCriticalSection(&clientLogSection);
	if (useEvent) {
		SetEvent(hConsoleLogEvent);
	}
//	LogOut("290", text);
}

CONSOLE_LOG * _stdcall GetConsoleLog()
{
//	LogOut("gcl", "295");
	if (useEvent) {
		bool yesWait = true;
		if (console_log != NULL) {
			if (console_log->next != NULL) {
				yesWait = false;
			}
		}
		if (yesWait) {
			WaitForSingleObject(hConsoleLogEvent, INFINITE);
		}
	}
	if (console_log == NULL) {
		return NULL;
	}
// COMMAND_RESULT new, into the nextResult.
// The Result is no one in the last command_result, which must be kept in memory
	if (console_log->next == NULL) {
		return NULL;
	}
//	LogOut("gcl", "307");
	EnterCriticalSection(&clientLogSection);
	if (console_log->log.text != NULL) {
		free(console_log->log.text);
	}
//	LogOut("gcl", "311");
	CONSOLE_LOG_CHAIN * next = console_log->next;
//	LogOut("gcl", "313");
	free(console_log);
//	LogOut("gcl", "315");
	console_log = next;
	if (console_log->next == NULL && useEvent) {
		ResetEvent(hConsoleLogEvent);
	}
//	LogOut("gcl", "320");
//	LogOut("dbg", console_log->log.text);
	LeaveCriticalSection(&clientLogSection);
	return &console_log->log;
}

const char * _stdcall GetConsoleLogText()
{
	CONSOLE_LOG * result = GetConsoleLog();
	if (result == NULL) {
		return NULL;
	}
	return result->text;
}

void ServerLogHandler(const char * log)
{
	callUI(TUNNEL_EVENT_SERVERLOG, 0, log);
}

DLLAPI void _stdcall SetClientLogHandler(tunnel_handler handler)
{
	theCallUI = handler;
}

char szUserId[33];

const char * GetUserId()
{
	char * result = NULL;
	//HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion
	HKEY hkResult[5];
	hkResult[0] = HKEY_LOCAL_MACHINE;
	char * keys[4] = {"SOFTWARE", "Microsoft", "Windows", "CurrentVersion"};

	for (int i = 0; i < 4; i++) {
		if (RegOpenKeyEx(hkResult[i], keys[i], 0, KEY_READ, &hkResult[i+1]) != ERROR_SUCCESS) {
			for (int j = 1; j < i; j++) {
				RegCloseKey(hkResult[j]);
			}
//			printf("126 Useless, I could not open key (i=%d)\n", i);
			return NULL;
		}
	}

	u_char data[30];
	DWORD dataSize = 30;

	if (RegQueryValueEx(hkResult[4], "ProductId", NULL, NULL, data, &dataSize) == ERROR_SUCCESS) {
	    result = szUserId;
//	    result = data;
 	    CMakeMD5 * md5 = new CMakeMD5();
        md5->ComputeHash(data, strlen(data));
	    md5->GetHashString(szUserId, TRUE);
    }

	// Open key handle
	for (int j = 1; j < 4; j++) {
		RegCloseKey(hkResult[j]);
	}
	
	return result;
}

const char * GetUserIdVista()
{
	char * result = NULL;
	//HKEY_LOCAL_MACHINE\SoftWare\Microsoft\Windows\CurrentVersion\ProductId
	//HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion
	HKEY hkResult[5];
	hkResult[0] = HKEY_LOCAL_MACHINE;
	char * keys[4] = {"SOFTWARE", "Microsoft", "Windows NT", "CurrentVersion"};

	for (int i = 0; i < 4; i++) {
		if (RegOpenKeyEx(hkResult[i], keys[i], 0, KEY_READ, &hkResult[i+1]) != ERROR_SUCCESS) {
			for (int j = 1; j < i; j++) {
				RegCloseKey(hkResult[j]);
			}
//			printf("126 Useless, I could not open key (i=%d)\n", i);
			return NULL;
		}
	}

	u_char data[30];
	DWORD dataSize = 30;

	if (RegQueryValueEx(hkResult[4], "ProductId", NULL, NULL, data, &dataSize) == ERROR_SUCCESS) {
	    result = szUserId;
//	    result = data;
 	    CMakeMD5 * md5 = new CMakeMD5();
        md5->ComputeHash(data, strlen(data));
	    md5->GetHashString(szUserId, TRUE);
    }

	// Open key handle
	for (int j = 1; j < 4; j++) {
		RegCloseKey(hkResult[j]);
	}
	
	return result;
}

bool GetDeviceDescription(const char * name, char * buffer, int size)
{
	//HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\NetworkCards
	//Since entering the bottom of and return to get it
	
	char serviceName[512];
	strcpy(serviceName, &name[12]);

	// RegOpenKeyEx http://msdn.microsoft.com/ja-jp/library/cc429950.aspx
	// RegCloseKey  http://msdn.microsoft.com/ja-jp/library/cc429930.aspx
//	printf("%s I'll to get a description of the\n", name);
	HKEY hkResult[7];
	char * keys[6] = {"SOFTWARE", "Microsoft", "Windows NT", "CurrentVersion", "NetworkCards"};
	int i;
	hkResult[0] = HKEY_LOCAL_MACHINE;
	for (i = 0; i < 6; i++) {
		if (RegOpenKeyEx(hkResult[i], keys[i], 0, KEY_READ, &hkResult[i+1]) != ERROR_SUCCESS) {
			for (int j = 1; j < i; j++) {
				RegCloseKey(hkResult[j]);
			}
//			printf("126 Useless, I could not open key (i=%d)\n", i);
			return false;
		}
	}

	// RegEnumKeyEx  http://msdn.microsoft.com/ja-jp/library/cc429912.aspx
	DWORD index = 0;
	char subKeyName[32];
	DWORD subKeyNameSize = 32;
	FILETIME fileTime;
	char * strDescription = "Description";
	char * strServiceName = "ServiceName";
	u_char data[512];
	DWORD dataSize;
	bool findIt = false;
	while (RegEnumKeyEx(hkResult[6], index, subKeyName, &subKeyNameSize, NULL, NULL, NULL, &fileTime) != ERROR_NO_MORE_ITEMS && !findIt) {
		//printf("The key: %s\n", subKeyName);
		HKEY hkSubKey;
		if (RegOpenKeyEx(hkResult[6], subKeyName, 0, KEY_READ, &hkSubKey) == ERROR_SUCCESS) {
			dataSize = (DWORD)sizeof(data);
			if (RegQueryValueEx(hkSubKey, strServiceName, NULL, NULL, data, &dataSize) == ERROR_SUCCESS) {
				if (strcmp(data, serviceName) == 0) {
					dataSize = (DWORD)size;
					if (RegQueryValueEx(hkSubKey, strDescription, NULL, NULL, buffer, &dataSize) == ERROR_SUCCESS) {
						findIt = true;
					}
				}
			}
			RegCloseKey(hkSubKey);
		}
		subKeyNameSize = 32;
		index++;
	}
	// Open key handle
	for (int j = 1; j < i; j++) {
		RegCloseKey(hkResult[j]);
	}
	return findIt;
}

DWORD WINAPI PacketCapture(void *)  
{
	// Sleep(0) I wonder if he 's OK I have not only put・・・
	int ret;
	const u_char *pkt_data;
	struct pcap_pkthdr *pkt_header;
	while(adapterStatus == 3) {
		
//		if (LinkUpDBM < Rssi) {
		
//			EnterCriticalSection(&pcapSection);
			if(0<=(ret=pcap_next_ex(adhandle,&pkt_header, &pkt_data))){
//				LeaveCriticalSection(&pcapSection);
			    if(ret==1){
			    	// Packet was read without a hitch
					// To pkt_header-> caplen at this time
					// Contains the data that was received pkt_data number of bytes received.
				    //PrintClientLog(0, "PacketCapture!!!");
					packet_handler(NULL, pkt_header, pkt_data);
					Sleep(0);
			    }
			} else {
//				LeaveCriticalSection(&pcapSection);
			}
//		}
	}
    //PrintClientLog(0, "PacketCapture end...");
	return 0;
}

bool CloseDevice()
{
	if (adapterStatus == 0) {
		return false;
	}
	adapterStatus = 1;
	WaitForSingleObject(packetCaptureHandle, 0);
	WaitForSingleObject(ssidMonitorHandle, 0);
	if (m_lpAdapter != NULL) {
		PacketCloseAdapter(m_lpAdapter);
		m_lpAdapter = NULL;
	}
	if (adhandle != NULL) {
		pcap_close(adhandle);
		adhandle = NULL;
	}
	adapterStatus = 0;
	return true;
}

bool EnableAdHook();
DWORD WINAPI MonitorSSID(void *);

bool CloseConnect()
{
	if (clientStatus > 1) {
	    needReconnect = false;
	
//	    DATA_HEADER dh;
//		char sendData[kBufferSize];
//		snprintf(sendData, sizeof(sendData), "%s がログアウトしました", szNickName);
//		dh.dtype = 'C';
//		dh.dsize = (short) strlen(sendData);
//		SendCommand(&gsd, &dh, sendData);
	
		CloseConnection(&gsd);
	}
    clientStatus = 0;
    return true;
}


bool OpenConnect(const char * _server)
{
	char server[512];
	strcpy(server, _server);
	int port = nPort;
	if (clientStatus > 0) {
		CloseConnect();
	}
	gsd.doCommand = doClientCommand;
	gsd.doClose = doClientClose;
	gsd.doConnect = doClientConnect;
	
	clientStatus = 1;
	bool connected = false;
	if (strlen(server) > 0) {

		char * kPort = (char*)memchr( server, ':', strlen(server) );
		if (kPort != NULL) {
			*kPort = '\0';
			kPort++;
			sscanf(kPort, "%d", &port);
		}

//	    cout << "名前解決をします..." << server << "..." << flush;
	    nRemoteAddress = LookupAddress(server);
	    if (nRemoteAddress == INADDR_NONE) {
//	        cerr << endl << WSAGetLastErrorMessage("名前解決に失敗しました。") << 
//	                endl;
	    } else {
		    in_addr Address;
		    memcpy(&Address, &nRemoteAddress, sizeof(u_long)); 
//		    cout << inet_ntoa(Address) << ":" << port << endl;
		    
		    // Connect to the server
		    callUI(TUNNEL_EVENT_NOTICE, 0, "I am connecting to the server ...");
//		    cout << "サーバーに接続中です..." << endl;
		    if (!EstablishConnection(&gsd, nRemoteAddress, htons(port))) {
			    callUI(TUNNEL_EVENT_NOTICE, 0, "I failed to connect to server.");
//			    cerr << endl << WSAGetLastErrorMessage("接続に失敗しました。") << endl;
		    } else {
		    	connected = true;
		    }
	    }
	}
	if (connected) {
		clientStatus = 2;
		SetSetting("Server", server);
		char tmp[10];
		snprintf(tmp, sizeof(tmp), "%d", port);
		SetSetting("Port", tmp);

//	    DATA_HEADER dh;
//	
//		char sendData[kBufferSize];
//		snprintf(sendData, sizeof(sendData), "%s がログインしました", szNickName);
//		dh.dtype = 'C';
//		dh.dsize = (short) strlen(sendData);
//		SendCommand(&gsd, &dh, sendData);
	} else {
		clientStatus = 0;
	}
	return connected;
}

bool OpenDevice(const char * deviceName)
{
	if (adapterStatus > 0) {
		CloseDevice();
	}
	adapterStatus = 1;

	char device[512];
	if (deviceName == NULL) {
		 GetSetting("Device", "", device, sizeof(device));
	} else {
		strcpy(device, deviceName);
		strcpy(szDevice, deviceName);
	}
	
	if (strlen(device) == 0) {
		adapterStatus = 0;
		return false;
	}
	
	adhandle = NULL;
//	adhandle = pcap_open_live(device,		// name of the device
//							 65536,			// portion of the packet to capture. 
//											// 65536 grants that the whole packet will be captured on all the MACs.
//							 1,				// promiscuous mode (nonzero means promiscuous)
//							 100,			// read timeout
//							 errbuf			// error buffer
//							 );
//	if (adhandle == NULL) {
//		callUI(TUNNEL_EVENT_NOTICE, 0, "デバイスをオープンできませんでした。");
//		adapterStatus = 0;
//		return false;
//	}
	
	//PrintClientLog(0, "無線LANデバイスを初期化しています。");
	m_lpAdapter = PacketOpenAdapter(device);
	if (m_lpAdapter == NULL) {
		callUI(TUNNEL_EVENT_NOTICE, 0, "I could not open the device.");
		adapterStatus = 0;
		return false;
	}
	Sleep(200);
	pktGet802_11SSID(m_lpAdapter, &Ssid);
	Sleep(200);
	
	// Disconnect the network being established
//	pktExec802_11Disassociate(m_lpAdapter);
//	Sleep(200);

	// Ad-Hook モードへ移行
	if ( FALSE == EnableAdHook() )
	{
		callUI(TUNNEL_EVENT_NOTICE, 0, "I can not be migrated to the ad-hoc mode.");
		PacketCloseAdapter(m_lpAdapter);
		adapterStatus = 0;
		return false;
	}
	
	SetSetting(_T("Device") , device);
	
	strcpy(szDevice, device);

	adapterStatus = 2;
	
	// Start packet capture
//    packetCaptureHandle = CreateThread(0, 0, PacketCapture, (void*)&gsd, 0, NULL);

	// SSID monitoring start
    ssidMonitorHandle = CreateThread(0, 0, MonitorSSID, NULL, 0, NULL);
    
	callUI(TUNNEL_EVENT_NOTICE, 0, "I have to initialize the device.");
    return true;
}

void LoadClientSetting()
{
	GetSetting(_T("Server"), _T(""), szServer, sizeof(szServer));
	SetSetting(_T("Server"), szServer);
	GetSetting(_T("Port"), _T("443"), szPort, sizeof(szPort));
	SetSetting(_T("Port"), szPort);
	GetSetting(_T("Device"), _T(""), szDevice, sizeof(szDevice));
	SetSetting(_T("Device"), szDevice);
	GetSetting(_T("NickName"), _T(""), szNickName, sizeof(szNickName));	
	SetSetting(_T("NickName"), szNickName);

    nPort = atoi(szPort);

	TCHAR szTmp[30];
	bool result = false;

	GetSetting(_T("SSIDAutoSence"), _T("false"), szTmp, sizeof(szTmp));	
	SetSetting(_T("SSIDAutoSence"), szTmp);
	if (strcmp("true", szTmp) == 0) {
		result = true;
	}
	ssidAutoSence = result;
	GetSetting(_T("SSIDAutoSenceInterval"), _T("10000"), szTmp, sizeof(szTmp));	
	SetSetting(_T("SSIDAutoSenceInterval"), szTmp);
	sscanf(szTmp, "%d", &ssidAutoSenceInterval);
	// I can not do more than 5 seconds
	if (ssidAutoSenceInterval < 5000) {
		ssidAutoSenceInterval = 5000;
	}

	result = false;
	GetSetting(_T("PSPAutoSence"), _T("true"), szTmp, sizeof(szTmp));	
	SetSetting(_T("PSPAutoSence"), szTmp);
	if (strcmp("true", szTmp) == 0) {
		result = true;
	}
	pspAutoSence = result;
	GetSetting(_T("PSPAutoSenceInterval"), _T("4000"), szTmp, sizeof(szTmp));	
	SetSetting(_T("PSPAutoSenceInterval"), szTmp);
	sscanf(szTmp, "%d", &pspAutoSenceInterval);
	// I can not do less than one second
	if (pspAutoSenceInterval < 1000) {
		pspAutoSenceInterval = 1000;
	}

	GetSetting(_T("logdir"), _T("logs"), szTmp, sizeof(szTmp));	
	SetSetting(_T("logdir"), szTmp);
	GetSetting(_T("debug"), _T("false"), szTmp, sizeof(szTmp));	
	SetSetting(_T("debug"), szTmp);
}

bool SendChat(SOCKET_EX * sdex, char * message)
{
    DATA_HEADER dh;
	dh.dtype = 'C';
	dh.dsize = (short) strlen(message);
	return SendCommand(sdex, &dh, message);
}

bool GetSSIDSetting(char* key, char* buffer, int size);

bool SendSSID(SOCKET_EX * sdex)
{
	if (adapterStatus > 1) {
		//SendChat(&gsd, "SendSSID");
		//pktGet802_11SSID(m_lpAdapter, &Ssid);
	}
	
	//Change notification SSID
	DATA_HEADER dh;
	USER_INFO ui;
	ui.last = clock();
	strcpy(ui.szNickName, szNickName);
	ZeroMemory(ui.szSSID, sizeof(ui.szSSID));
//	if (last_psp_packet + roomWait > clock() && last_psp_packet != 0) {
		memcpy(ui.szSSID, Ssid.Ssid, Ssid.SsidLength);
		GetSSIDSetting(ui.szSSID, room, sizeof(room));
//	}
	ui.majorVersion = MAJOR_VERSION;
	ui.minorVersion = MINOR_VERSION;
	dh.dtype = 'S';
	dh.dsize = (short)sizeof(USER_INFO);

	int pspCount = 0;
	for (int i = 0; i < maxMacAddr; i++) {
		if (mac[i].active) {
			pspCount++;
		}
	}
	
	ui.pid = GetCurrentProcessId();
	
	ui.pspCount = pspCount;
	const char * userId = GetUserId();
	if (userId != NULL) {
		strcpy(ui.szUID, userId);
	} else {
		userId = GetUserIdVista();
		if (userId != NULL) {
			strcpy(ui.szUID, userId);
		} else {
			strcpy(ui.szUID, szNickName);
		}
	}
	
	return SendCommand(sdex, &dh, (char *)&ui);
}

//
// To set up ad hoc for PSP
//
bool EnableAdHook()
{
	//if(!pktExec802_11Disassociate(m_lpAdapter)) 
	//	return false;
	//Sleep(200);

	// Switch to Ad-Hook Network
    if (!pktSet802_11NetworkMode(m_lpAdapter, Ndis802_11IBSS))
		return false;
	Sleep(200);

	// Disable the WEP
    if (!pktSet802_11WEPStatus(m_lpAdapter, Ndis802_11WEPDisabled))
		return false;
	Sleep(200);

	//　Switch to Open Authentication
    if (!pktSet802_11AuthMode(m_lpAdapter, Ndis802_11AuthModeOpen))
		return false;
	Sleep(200);

	return true;
}

/**
 * Reads the configuration file
 */ 
bool GetSSIDSetting(char* key, char* buffer, int size)
{
	char	cur[512];
	TCHAR	* fileName = "ini";
	if ( GetModuleFileName(NULL, cur, sizeof(cur)) == 0 ) {
		// Error
	}
	strcpy(&cur[strlen(cur) - 3], fileName);
	
	// The default SSID
	GetPrivateProfileString( 
		_T("SSID") , key , key ,
		buffer , size , cur );
	
	if (memcmp(key, "PSP_", 4) == 0) {
		return true;
	}
	
	return false;
}

/**
 * Reads the configuration file
 */ 
bool GetMACSetting(MAC_ADDRESS * mac, char* buffer, int size)
{
	char	cur[512];
	TCHAR	* fileName = "ini";
	if ( GetModuleFileName(NULL, cur, sizeof(cur)) == 0 ) {
		// Error
	}
	strcpy(&cur[strlen(cur) - 3], fileName);

	// The default MAC address
	char tmp[10];
	int itmp = 0;
	GetPrivateProfileString( 
		_T("MAC") , "PSPCount" , "0" ,
		tmp , sizeof(tmp) , cur );
	sscanf(tmp, "%d", &itmp);
	itmp++;
	
	char	key[512];
	snprintf(key, sizeof(key), "%02X:%02X:%02X:%02X:%02X:%02X", mac->addr[0], mac->addr[1], mac->addr[2], mac->addr[3], mac->addr[4], mac->addr[5]);
	char	def[512];
	if (itmp > 1) {
		snprintf(def, sizeof(def), "%s の PSP(%d)", szNickName, itmp);
	} else {
		snprintf(def, sizeof(def), "%s", szNickName);
	}
	char * notfound = "!NOTFOUND!";
	
	// The default MAC address
	GetPrivateProfileString( 
		_T("MAC") , key , notfound ,
		buffer , size , cur );
	
	if (strcmp(notfound, buffer) == 0) {
		if (pspAutoSence) {
			snprintf(tmp, sizeof(tmp), "%d", itmp);
			WritePrivateProfileString( 
				_T("MAC") , key , def , cur );
			WritePrivateProfileString( 
				_T("MAC") , "PSPCount" , tmp , cur );
			strcpy(buffer, def);
		} else {
		//PrintClientLog(0, "GetMACSetting - NF");
			return false;
		}
	}	
	//PrintClientLog(0, "GetMACSetting - Found");
	return true;
}

bool inTheRoom = false;

// SSID monitor thread
DWORD WINAPI MonitorSSID(void *)
{
//	DATA_HEADER dh;
//	char data;
//	dh.dsize = 0;
//	dh.dtype = 'P';
//	SOCKET_EX * sdex = &gsd;
	bool findPSP;
	while (adapterStatus > 1) {
//		findPSP = false;

//		Sleep(roomWait);
//		SendSSID(&gsd);

		for (int i = 0; i < maxMacAddr; i++) {
			if (mac[i].last_clock + roomWait <= clock() && mac[i].active) {
				char psp[512];
				char comment[512];
//				if (GetMACSetting(&mac[i], psp, sizeof(psp))) {
//					snprintf(comment, sizeof(comment), "%s が、%s から出ました %d (%d)", psp, room, clock(), (int)mac[i].last_clock + roomWait - clock());
//					SendChat(&gsd, comment);
//				}
				mac[i].active = false;
//				SendSSID(&gsd);
				SendSSID(&gsd);
			}
//			if (mac[i].last_clock + roomWait > clock()) {
// 				findPSP = true;
//			}
		}

//		SendChat(&gsd, "pktGet802_11RSSI 803");
		
		if (!inTheRoom) {
			if (m_lpAdapter) {
				pktGet802_11RSSI(m_lpAdapter, &Rssi);
			}
			Sleep(50);
			if (Rssi > LinkUpDBM) {
				if (m_lpAdapter) {
					pktGet802_11SSID(m_lpAdapter, &Ssid);
				}
				Sleep(50);
				char buf[33];
				ZeroMemory(buf, 33);
				CopyMemory(buf, Ssid.Ssid, Ssid.SsidLength);

				if (GetSSIDSetting(buf, room, sizeof(room))) {
					char comment[512];
//					SendSSID(&gsd);
//					snprintf(comment, sizeof(comment), "%s が、%s に入りました %d", szNickName, buf, Rssi);
//					SendChat(&gsd, comment);
					inTheRoom = true;
					
					adhandle = pcap_open_live(szDevice,		// name of the device
											 65536,			// portion of the packet to capture. 
															// 65536 grants that the whole packet will be captured on all the MACs.
											 1,				// promiscuous mode (nonzero means promiscuous)
											 100,			// read timeout
											 errbuf			// error buffer
											 );
					
					adapterStatus = 3;
					last_psp_packet = clock();
					packetCaptureHandle = CreateThread(0, 0, PacketCapture, (void*)&gsd, 0, NULL);
					
				}
			}
		} else {
			if (last_psp_packet + roomWait < clock()) {
				adapterStatus = 2;
				WaitForSingleObject(packetCaptureHandle, 0);
				if (adhandle != NULL) {
					pcap_close(adhandle);
					adhandle = NULL;
				}
//				char comment[512];
//				snprintf(comment, sizeof(comment), "%s が、%s から出ました %d", szNickName, room, Rssi);
//				SendChat(&gsd, comment);
				inTheRoom = false;				
			}
		}
		
		if (!inTheRoom && ssidAutoSence) {
			Sleep(ssidAutoSenceInterval);
		}
		if (!inTheRoom && ssidAutoSence) {

//			EnterCriticalSection(&pcapSection);

			// PSPが1台も見つからないなら、SSIDスキャンを行う

			// CloseAdapterとOpenAdapterを連続でやると不安定になるようだ
			//PacketCloseAdapter(m_lpAdapter);
			//m_lpAdapter = PacketOpenAdapter(szDevice);

			char buf[33];
//			if (TRUE == pktGet802_11SSID(m_lpAdapter, &Ssid)) {
//				ZeroMemory(buf, 33);
//				CopyMemory(buf, Ssid.Ssid, Ssid.SsidLength);
//				printf("今のSSIDは %s です \n", buf);
//			} else {
//				printf("今のSSIDの取得に失敗\n");
//			}

			if (m_lpAdapter==NULL) {
				break;
			}
//			SendChat(&gsd, "pktExec802_11BSSIDScan");
			// SSID スキャン時の処理を記述
			if (TRUE == pktExec802_11BSSIDScan(m_lpAdapter)) {
//				printf("pktExec802_11BSSIDScan 成功\n");
				
//				Sleep(2000);
				Sleep(200);
	
//				printf("SSIDスキャン中... \n");
				// SSID リスト取得時の処理を記述
				if ( TRUE == pktGet802_11BSSIDList(m_lpAdapter, m_Bssid, MAX_BSSID) )
				{	
					// BSSID上のSSIDとベースSSIDを比較し,マッチした場合は切り替える
					for ( int i = 0; (i < MAX_BSSID) && (0 != m_Bssid[i].Length); i++ )
					{
						ZeroMemory(buf, 33);
						CopyMemory(buf, m_Bssid[i].Ssid.Ssid, m_Bssid[i].Ssid.SsidLength);
	
						char name[40];
						if (GetSSIDSetting(buf, name, sizeof(name))) {

//							snprintf(pb, sizeof(pb), "%s に移動", name);
//							callUI(TUNNEL_EVENT_NOTICE, 0, pb);

							//printf("%s は INI にあった\n", buf);
	
							// 確立中のネットワークを切断
//							pktExec802_11Disassociate(m_lpAdapter);
	
							NDIS_802_11_CONFIGURATION Config;
							if ( FALSE == pktGet802_11Configuration(m_lpAdapter, &Config) )
							{
								printf("pktGet802_11Configuration Failure \n");
								LeaveCriticalSection(&pcapSection);
								break; // Failure
							}
	
							Config.DSConfig = m_Bssid[i].Configuration.DSConfig;
							memcpy(&Config, &m_Bssid[i].Configuration, sizeof(NDIS_802_11_CONFIGURATION));
							if ( FALSE == pktSet802_11Configuration(m_lpAdapter, &Config) )
							{
								snprintf(pb, sizeof(pb), "%s (Go to the pktSet802_11Configuration2 Failure", name);
								callUI(TUNNEL_EVENT_NOTICE, 0, pb);
								//break; // Failure
							}
							
							Sleep(200);
	
							// Apply the new SSID
							if ( TRUE == pktSet802_11SSID(m_lpAdapter, &m_Bssid[i].Ssid))
							{
								ZeroMemory(&Ssid, sizeof(NDIS_802_11_SSID));
								memcpy(&Ssid, &m_Bssid[i].Ssid, sizeof(NDIS_802_11_SSID));
//								printf("pktSet802_11SSID成功 \n");

//								pcap_close(adhandle);
//								adhandle = pcap_open_live(szDevice,		// name of the device
//														 65536,			// portion of the packet to capture. 
//																		// 65536 grants that the whole packet will be captured on all the MACs.
//														 1,				// promiscuous mode (nonzero means promiscuous)
//														 100,			// read timeout
//														 errbuf			// error buffer
//														 );
//								if (adhandle == NULL) {
//									callUI(TUNNEL_EVENT_NOTICE, 0, "デバイスをオープンできませんでした。");
//								}
								LeaveCriticalSection(&pcapSection);
								strcpy(room, name);
								
						    	//SendSSID(&gsd);
//								Sleep(roomWait);
							} else {
								LeaveCriticalSection(&pcapSection);
								snprintf(pb, sizeof(pb), "pktSet802_11SSID Failure");
								callUI(TUNNEL_EVENT_NOTICE, 0, pb);
							}
							
//							if ( TRUE == pktGet802_11SSID(m_lpAdapter, &Ssid)) {
//								printf("pktGet802_11SSID成功 \n");
//							} else {
//								printf("pktGet802_11SSID失敗 \n");
//							}
//							Sleep(ssidAutoSenceInterval);
							break; // I do not compare any more since the change
						} else {
//							printf("%s は INI にない\n", buf);
						}
					}
				}
				LeaveCriticalSection(&pcapSection);

	
			} else {
				SendChat(&gsd, "pktExec802_11BSSIDScan Failure");
				PacketCloseAdapter(m_lpAdapter);
				Sleep(10);
				m_lpAdapter = PacketOpenAdapter(szDevice);				
//				printf("pktExec802_11BSSIDScan Failure\n");
			}
		} else {
			Sleep(1000);
		}
    };
    return 0;
}

/**
* Callback function to process the received packet
? * Sdex - communication content
? * Dh - data received header
? * Data - data reception
 */
void doClientCommand(SOCKET_EX * sdex, const DATA_HEADER * _dh, const char * data)
{
	DATA_HEADER rdh;
	DATA_HEADER * dh = &rdh;
	memcpy(dh, _dh, sizeof(DATA_HEADER));

	//printf("%d から '%c'コマンド (%d バイト) 受信。\n", (int)dh.doption, dh.dtype, (int)dh.dsize);
	if (dh->dtype == 't') {

		// If Type is 88 C8, automatically recognize up to 4
		int findMac = -1;
		for (int i = 0; i < maxiMacAddr; i++) {
//			if (ignoreMac[i].last_clock + pspAutoSenceInterval > clock()) {
			if (ignoreMac[i].active) {
				if (memcmp((char *)&ignoreMac[i], &data[6], 6) == 0) {
					findMac = i;
					break;
				}
			}
		}
		if (findMac == -1) {
			for (int i = 0; i < maxiMacAddr; i++) {
//				if (ignoreMac[i].last_clock + pspAutoSenceInterval <= clock()) {
				if (!ignoreMac[i].active) {
					memcpy((char *)&ignoreMac[i], &data[6], 6);
					ignoreMac[i].last_clock = clock();
					ignoreMac[i].active = true;
					//printf("遠隔PSP認識(%d): MACアドレス = %0X:%0X:%0X:%0X:%0X:%0X\n", i + 1, ignoreMac[i].addr[0], ignoreMac[i].addr[1], ignoreMac[i].addr[2], ignoreMac[i].addr[3], ignoreMac[i].addr[4], ignoreMac[i].addr[5]);
					break;
				}
			}
		}

		if (adapterStatus > 2) {
			pcap_sendpacket(adhandle, data, (int)dh->dsize);
		}
	} else if (dh->dtype == 'c') {
		char sdata[4096];
		memcpy(sdata, data, (int)dh->dsize);
		sdata[(int)dh->dsize] = 0;
//		printf("%s (S%d->R%d S%d/R%d)\n", data, sdex->sendCount, dh->recvCount, SendPacketSize(sdex), RecvPacketSize(sdex));
		char debugini[40];
		GetSetting("debug", "false", debugini, sizeof(debugini));
		if (strcmp(debugini, "true") == 0) {		
			snprintf(pb, sizeof(pb), "%s (S%d->R%d S%d/R%d)", sdata, sdex->sendCount, dh->recvCount, SendPacketSize(sdex), RecvPacketSize(sdex));
			callUI(TUNNEL_EVENT_CLIENTLOG, 0, pb);
		} else {
			callUI(TUNNEL_EVENT_CLIENTLOG, 0, sdata);
		}
    } else if (dh->dtype == 'P') {
    	dh->dtype = 'p';
        if (SendCommand(sdex, dh, data)) {
//			printf("ping応答(%d/%d)\n", sdex->sendCount, sdex->recvCount);
        } else {
//			printf("ping応答失敗\n");
        }
//		printf("ping(S%d/%d R%d/%d)\n", dh->sendCount, sdex->sendCount, dh->recvCount ,sdex->recvCount);
	} else if (dh->dtype == 'p') {
		if ((int)dh->dsize == sizeof(clock_t)) {
			sdex->info.ping = clock() - (clock_t)data;
		}
//		data[(int)dh->dsize] = 0;
//		printf("ping(S%d->R%d) (Last: S%d R%d)\n", sdex->sendCount, dh->recvCount, SendPacketSize(sdex), RecvPacketSize(sdex));
//		static char dummyPacket[16];
//		
//		ZeroMemory(dummyPacket, 16);
//		if (adapterStatus > 2) {
//			//pcap_sendpacket(adhandle, dummyPacket, 16);
//		}

	} else if (dh->dtype == 'u') {

		// Response to the command user confirmation
		USER_INFO ui;
		memcpy(&ui.last, data, sizeof(clock_t));
		strcpy(ui.szNickName, szNickName);
		ZeroMemory(ui.szSSID, sizeof(ui.szSSID));
		if (last_psp_packet + roomWait > clock() && last_psp_packet != 0) {
			memcpy(ui.szSSID, Ssid.Ssid, Ssid.SsidLength);
			GetSSIDSetting(ui.szSSID, room, sizeof(room));
		}
		ui.majorVersion = MAJOR_VERSION;
		ui.minorVersion = MINOR_VERSION;
		dh->dtype = 'I';
		dh->dsize = (short)sizeof(USER_INFO);
		SendCommand(sdex, dh, (char *)&ui);

	} else if (dh->dtype == 'i') {

		// Receiving the results of the command user confirmation
		USER_INFO ui;
		memcpy(&ui, data, sizeof(USER_INFO));
		clock_t now = clock();
//		printf("%d: %s (Ping: %d)\n", (int)dh->doption, ui.szNickName, (now - ui.last));
		char roomName[256];
		GetSSIDSetting(ui.szSSID, roomName, sizeof(roomName));
		snprintf(pb, sizeof(pb), "%d: %s (Ping: %d) %s", (int)dh->doption, ui.szNickName, (now - ui.last), roomName);
		callUI(TUNNEL_EVENT_NOTICE, 0, pb);

	} else if (dh->dtype == 'a') {

		// Receiving the results of the command user confirmation
		USER_INFO * ui = (USER_INFO *)data;
		int count = (int)dh->doption;

//		snprintf(pb, sizeof(pb), "aコマンド ユーザー%d人", count);
//		callUI(TUNNEL_EVENT_NOTICE, 0, pb);

		bool logout[maxUsers];
		for (int i = 0; i < maxUsers; i++) {
			logout[i] = user_active[i];
		}
		for (int i = 0; i < count; i++) {
//			snprintf(pb, sizeof(pb), " %d : %s", i, ui[i].szNickName);
//			callUI(TUNNEL_EVENT_NOTICE, 0, pb);
			bool find = false;
			for (int c = 0; c < maxUsers; c++) {
				if (user_active[c]) {
					// Comparison process ID
					if (ui[i].pid == users[c].pid) {
						// Compare product ID
						if (strcmp(ui[i].szUID,users[c].szUID) == 0) {
							// Turn out to be the same
							// SSID does not compare

							// Comparing the number of PSP
							if (ui[i].pspCount != users[c].pspCount) {
								// Number has changed
								if (ui[i].pspCount == 0) {
									// Number reaches 0
									// Came out of the meeting place that is
									char roomName[50];
									if (GetSSIDSetting(users[c].szSSID, roomName, sizeof(roomName))) {
										snprintf(pb, sizeof(pb), "%s The %s I came out of the", ui[i].szNickName, roomName);
										callUI(TUNNEL_EVENT_NOTICE, 0, pb);
									}
								} else if (users[c].pspCount == 0) {
									char roomName[50];
									if (GetSSIDSetting(ui[i].szSSID, roomName, sizeof(roomName))) {
										snprintf(pb, sizeof(pb), "%s The %s I went into the", ui[i].szNickName, roomName);
										callUI(TUNNEL_EVENT_NOTICE, 0, pb);
									}
								}
							} // PSPの台数には変化なし
							
							memcpy(&users[c], &ui[i], sizeof(USER_INFO));
							logout[c] = false;
							find = true;
							break;
						}
					}
				}
			} // for (int c = 0; c < maxUsers; c++)
			if (!find) {
				for (int c = 0; c < maxUsers; c++) {
					if (!user_active[c]) {
						memcpy(&users[c], &ui[i], sizeof(USER_INFO));
						logout[c] = false;
						user_active[c] = true;
						snprintf(pb, sizeof(pb), "%s I have logged the", ui[i].szNickName);
						callUI(TUNNEL_EVENT_NOTICE, 0, pb);
						break;
					}
				}
			}
		}
		for (int i = 0; i < maxUsers; i++) {
			if (logout[i]) {
				snprintf(pb, sizeof(pb), "%s I have to log out is", users[i].szNickName);
				callUI(TUNNEL_EVENT_NOTICE, 0, pb);
				user_active[i] = false;
			}
		}
		callUI(TUNNEL_EVENT_ENTERUSER, 0, "");

	} else {
		snprintf(pb, sizeof(pb), "Command not supported'%c'(%d Byte)\n", dh->dtype, (int)dh->dsize);
		callUI(TUNNEL_EVENT_NOTICE, 0, pb);
		CloseConnection(sdex);
	}
}

void doClientClose(SOCKET_EX *)
{
	//SOCKET_EX * sdex = (SOCKET_EX *)_sdex;
	if (needReconnect == false) {
		return;
	}
	do {
		// Processing at the time of cutting
		if (gsd.state != STATE_WAITTOCONNECT) {
//			printf("再接続します...\n");
		}
	    //gsd = EstablishConnection(&gsd, nRemoteAddress, htons(nPort));
	    if (!EstablishConnection(&gsd, nRemoteAddress, htons(nPort))) {
//	        cerr << WSAGetLastErrorMessage("Reconnection failure.") << 
//	                endl;
		} else {
	    	SendSSID(&gsd);
	    }
	    Sleep(10);
	} while (gsd.state != STATE_CONNECTED);
}

void doClientConnect(SOCKET_EX * sdex)
{
    needReconnect = true;
	clientStatus = 2;

	// SSID change notification
	SendSSID(sdex);
}

COMMAND_RESULT * _stdcall TextCommand(const char * acReadBuffer)
{
	if (acReadBuffer == NULL) {
		return NULL;
	}

	COMMAND_RESULT * result = NULL;

	char tmpb[6];
	GetSetting("debug", "false", tmpb, sizeof(tmpb));
	if (strcmp(tmpb, "true")==0) {
		LogOut("cmd", acReadBuffer);
	}
	SOCKET_EX * sdex = &gsd;
    int nReadBytes;
    DATA_HEADER dh;

	if (strcmp(acReadBuffer, "/logout") == 0) {
		
	
	} else if (strcmp(acReadBuffer, "/help") == 0) {
		
		// Help command
		callUI(TUNNEL_EVENT_NOTICE, 2,"------------ Command general");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/status           View current status of");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/help              List of commands");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/exit              The end of the program");
		callUI(TUNNEL_EVENT_NOTICE, 2,"");
		callUI(TUNNEL_EVENT_NOTICE, 2,"Command Arena --------");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/say [Comment]                Remarks to the entire (Arena) server");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/party [Comment]              Remark only to members who are in the same meeting place");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/tell [Target] [Comment]        Remarks to the specified user");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/users                         Displays a list of logged-in users and Ping");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/connect Connected to the (arena) server: [port] server name []");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/close                         Disconnected from the (arena) server");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/opendevice [デバイス名]       Connected to the PSP via a wireless adapter");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/devicedesc [デバイス名]       Displays the description of the wireless adapter");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/listdevice                    Displays a list of wireless adapters");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/closedevice                   Disconnect the wireless adapter");
		callUI(TUNNEL_EVENT_NOTICE, 2,"");
		callUI(TUNNEL_EVENT_NOTICE, 2,"-------------- Other Settings");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/set SSIDAutoSence true             Enable the automatic search of SSID");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/set SSIDAutoSence false            To disable the automatic search of the SSID (default)");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/set SSIDAutoSenceInterval [Millisecond] Set the update interval for automatic search of SSID");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/set PSPAutoSence true              Enable the automatic search of PSP (the default)");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/set PSPAutoSence false             To disable the automatic search for PSP");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/set PSPAutoSenceInterval [Millisecond]  Sets the interval for automatic search is PSP");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/set debug true                     Debugging output");
		callUI(TUNNEL_EVENT_NOTICE, 2,"/set debug false                    No debug output (default)");

	} else if (strcmp(acReadBuffer, "/u") == 0) {
		
		if (clientStatus > 1) {

			// Users command
			for (int i = 0; i < maxUsers; i++) {
				if (user_active[i]) {
					char roomName[50];
					if (!GetSSIDSetting(users[i].szUID, roomName, sizeof(roomName))) {
						roomName[0] = 0;
					}
					snprintf(pb, sizeof(pb), "%s:%d", users[i].szUID, users[i].pid);
					AddCommandResult(&result, pb);
				}
			}
		}		

	} else if (memcmp(acReadBuffer, "/userinfo", 9) == 0) {
		
		char uid[50];
		sscanf(acReadBuffer, "/userinfo %s", uid);

		// userinfo command
		for (int i = 0; i < maxUsers; i++) {
			char tmp[50];
			snprintf(tmp, sizeof(tmp), "%s:%d", users[i].szUID, users[i].pid);
			if (strcmp(uid, tmp)==0) {
				snprintf(pb, sizeof(pb), "%s", users[i].szUID);
				AddCommandResult(&result, pb);

				snprintf(pb, sizeof(pb), "%d", users[i].pid);
				AddCommandResult(&result, pb);

				snprintf(pb, sizeof(pb), "%d.%d", users[i].majorVersion, users[i].minorVersion);
				AddCommandResult(&result, pb);
				
				AddCommandResult(&result, users[i].szNickName);
				
				AddCommandResult(&result, users[i].szSSID);

				char room[50];
				GetSSIDSetting(users[i].szSSID, room, sizeof(room));
				AddCommandResult(&result, room);

				snprintf(pb, sizeof(pb), "%d", users[i].ping);
				AddCommandResult(&result, pb);

				snprintf(pb, sizeof(pb), "%d", users[i].pspCount);
				AddCommandResult(&result, pb);
			}
		}

	} else if (strcmp(acReadBuffer, "/users") == 0) {
		
		// Users command
		int count = 0;
		for (int i = 0; i < maxUsers; i++) {
			if (user_active[i]) {
				count++;
				char roomName[50];
				if (!GetSSIDSetting(users[i].szSSID, roomName, sizeof(roomName))) {
					roomName[0] = 0;
				}
				snprintf(pb, sizeof(pb), "%d : %s (Ping %d) %s PSPx%d", count, users[i].szNickName, (int)users[i].ping, roomName, users[i].pspCount);
				AddCommandResult(&result, pb);
			}
		}
		SendSSID(&gsd);

	} else if (strcmp(acReadBuffer, "/usersold") == 0) {
		
		// Users Command

		clock_t pingClock = clock();
		char sendData[sizeof(clock_t)];
		
		dh.dtype = 'U';
		dh.dsize = sizeof(int);
		
		memcpy(sendData, (char *)&pingClock, sizeof(clock_t));
		
		SendCommand(&gsd, &dh, sendData);

	} else if (strcmp(acReadBuffer, "/dump") == 0) {
		
		// Dump command
		char sendData[10];
		
		dh.dtype = 'C';
		dh.dsize = (short)0;
		
		BufferDump(&gsd, &dh, sendData);

	} else if (memcmp(acReadBuffer, "/tell ", 6) == 0 || memcmp(acReadBuffer, "/t ", 3) == 0) {
		
		if (clientStatus > 1) {
			// Tell command
			dh.dtype = 'D';
			dh.dsize = strlen(acReadBuffer);
			
			SendCommand(sdex, &dh, acReadBuffer);
		} else {
			snprintf(pb, sizeof(pb), "I'm not connected to the arena");
			callUI(TUNNEL_EVENT_ERROR, 0, pb);
		}

	} else if (memcmp(acReadBuffer, "/party ", 7) == 0 || memcmp(acReadBuffer, "/p ", 3) == 0) {
		
		if (clientStatus > 1) {
			// Party command
			dh.dtype = 'Y';
			dh.dsize = (short)strlen(acReadBuffer);
			
			SendCommand(sdex, &dh, acReadBuffer);
		} else {
			snprintf(pb, sizeof(pb), "I'm not connected to the arena");
			callUI(TUNNEL_EVENT_ERROR, 0, pb);
		}

	} else if (strcmp(acReadBuffer, "/opendevice") == 0) {
		
		OpenDevice(NULL);
		
	} else if (memcmp(acReadBuffer, "/opendevice", 11) == 0) {
		
		char deviceName[200];
		sscanf(acReadBuffer, "/opendevice %s", deviceName);
		OpenDevice(deviceName);
		
	} else if (memcmp(acReadBuffer, "/usessid", 8) == 0) {
		
		char value[200];
		sscanf(acReadBuffer, "/usessid %s", value);
		if (value != NULL) {
			if (strcmp(value, "true")) {
				//UseSSIDFilter(true);
			} else if (strcmp(value, "false")) {
				//UseSSIDFilter(false);
			} else {
				snprintf(pb, sizeof(pb), "Invalid option:%s true Or false Please specify the",value);
				callUI(TUNNEL_EVENT_ERROR, 0, pb);
			}
		}
		
	} else if (strcmp(acReadBuffer, "/listdevice") == 0) {
		
		WIRELESS_LAN_DEVICE * devices = FindDevices();
		WIRELESS_LAN_DEVICE * device = devices;
		
		while (device != NULL) {
			AddCommandResult(&result, device->name);
			device = device->nextDevice;
		};
		
	} else if (memcmp(acReadBuffer, "/devicedesc", 11) == 0) {
		
		char deviceName[200];
		char buffer[200];
		sscanf(acReadBuffer, "/devicedesc %s", deviceName);
		
		GetDeviceDescription(deviceName, buffer, sizeof(buffer));
		AddCommandResult(&result, buffer);
		
	} else if (strcmp(acReadBuffer, "/closedevice") == 0) {
		
		if (CloseDevice()) {
			callUI(TUNNEL_EVENT_NOTICE, 0, "I closed the connection device.");
		}
		
	} else if (memcmp(acReadBuffer, "/test", 5) == 0) {
		
		if (lastDummyPacket == 0) {
			AddCommandResult(&result, "0");
		} else {
			char buffer[200];		
			snprintf(buffer, sizeof(buffer), "%d", (int)(clock() - lastDummyPacket));
			AddCommandResult(&result, buffer);
		}
		char dummyPacket[16];
		ZeroMemory(dummyPacket, 16);
		if (adapterStatus > 2) {
			pcap_sendpacket(adhandle, dummyPacket, 16);
		}
		
	} else if (memcmp(acReadBuffer, "/rssi", 5) == 0) {
		
		if (adapterStatus > 1) {
			pktGet802_11RSSI(m_lpAdapter, &Rssi);
			char buffer[200];
			
			snprintf(buffer, sizeof(buffer), "Rssi : %d", Rssi);
			AddCommandResult(&result, buffer);
		} else {
			AddCommandResult(&result, "Device not connected");
		}
		
	} else if (strcmp(acReadBuffer, "/version") == 0) {

		char tmp[10];
		snprintf(tmp, sizeof(tmp), "%d.%d", MAJOR_VERSION, MINOR_VERSION);
		AddCommandResult(&result, tmp);

	} else if (strcmp(acReadBuffer, "/close") == 0) {
		
		CloseConnect();
		
	} else if (memcmp(acReadBuffer, "/openserver", 11) == 0) {
		
		//ARENA arena;
		//GetServerSetting(&arena);
		int port = 0;
		sscanf(acReadBuffer, "/openserver %d", &port);
		char sPort[256];
		sscanf(acReadBuffer, "/openserver %s", sPort);
		if (port != 0) {
		//	arena.port = port;
		}
		//OpenServer(&arena);

		if (pinfo.hProcess != NULL) {
			TerminateProcess(pinfo.hProcess, 0);
			Sleep(100);
		}
		
		ZeroMemory(&sinfo,sizeof(sinfo));
		sinfo.cb=sizeof(sinfo);
		pinfo.hProcess = NULL;

		char szPath[_MAX_PATH];
		char szDrive[_MAX_DRIVE];
		char szDir[_MAX_DIR];
		char szFileName[_MAX_FNAME];
		char szExt[_MAX_EXT];
		char szOutput[_MAX_PATH * 5 + 1024];
		DWORD dwRet;
		
		//Initialization
		memset(szPath, 0x00, sizeof(szPath));
		memset(szDrive, 0x00, sizeof(szDrive));
		memset(szDir, 0x00, sizeof(szDir));
		memset(szExt, 0x00, sizeof(szExt));
		memset(szOutput, 0x00, sizeof(szOutput));
			
		//Gets the full path name of a running process
		dwRet = GetModuleFileName(NULL, szPath, sizeof(szPath));
		if(dwRet == 0) {
			
		}
			
		//Split the full path name
		_splitpath(szPath, szDrive, szDir, szFileName, szExt);
		
		TCHAR	fileName[512];
		TCHAR	comLine[512];
		SECURITY_ATTRIBUTES secAttr;
		
		char * szProgName = "TunnelSVR.exe";
	
		wsprintf(fileName,"%s%s%s", szDrive, szDir, szProgName);		
		wsprintf(comLine,"\"%s%s%s\" %d", szDrive, szDir, szProgName, port);		
		
		bool result = CreateProcess(
		  fileName,                 // The name of the executable module
		  comLine,                      // String of command-line
		  NULL, // Security descriptor
		  NULL,  // Security descriptor
		  TRUE ,                      // Handle inheritance option
		  CREATE_NO_WINDOW,                     // Flag of creating
		  NULL,                      // New environment block
		  NULL,                // The name of the current directory
		  &sinfo,  // Startup information
		  &pinfo // Process information
		);
		
		if (result) {
		Sleep(2000);

//		InitConnection(&gsd, 256);
//		gsd.doCommand = doClientCommand;
//		gsd.doClose = doClientClose;
//		gsd.doConnect = doClientConnect;

//		callUI(TUNNEL_EVENT_ERROR, 0, "1477:OpenServer");
//		callUI(TUNNEL_EVENT_ERROR, 0, "1479:OpenServer");
		//LocalConnect(&gsd);
//		callUI(TUNNEL_EVENT_ERROR, 0, "1484:OpenServer");

		clientStatus = 2;
		char tmp[10];
		snprintf(tmp, sizeof(tmp), "%d", port);
		SetSetting("Port", tmp);

//		callUI(TUNNEL_EVENT_ERROR, 0, "1491:OpenServer");

//	    DATA_HEADER dh;
//		char sendData[kBufferSize];
//		snprintf(sendData, sizeof(sendData), "%s がログインしました", szNickName);
//		dh.dtype = 'C';
//		dh.dsize = (short) strlen(sendData);
//		SendCommand(&gsd, &dh, sendData);

		}
		char ac[80];
		char tmp[100];
		if (gethostname(ac, sizeof(ac)) != SOCKET_ERROR) {
			snprintf(tmp, sizeof(tmp), "%s:%d", ac, port);
			OpenConnect(tmp);
	    }
		
	} else if (memcmp(acReadBuffer, "/closeserver", 11) == 0) {
		
		//int port;
		//sscanf(acReadBuffer, "/openserver %d", &port);
		//CloseServer();
		if (pinfo.hProcess != NULL) {
			TerminateProcess(pinfo.hProcess, 0);
		}
		
	} else if (memcmp(acReadBuffer, "/set ", 5) == 0) {
		
		char key[100];
		char value[512];
		sscanf(acReadBuffer, "/set %s %s", key, value);
		SetSetting(key, value);
		LoadClientSetting();
		
	} else if (memcmp(acReadBuffer, "/get ", 5) == 0) {
		
		char key[100];
		char value[512];
		sscanf(acReadBuffer, "/get %s", key);
		GetSetting(key, "", value, sizeof(value));
		AddCommandResult(&result, value);
		//callUI(TUNNEL_EVENT_NOTICE, 0, value);
		
	} else if (strcmp(acReadBuffer, "/connect internal") == 0) {
		
		//内部接続
		
	} else if (memcmp(acReadBuffer, "/connect", 8) == 0) {
		
		char server[512];
		if (sscanf(acReadBuffer, "/connect %s", server) == 1) {
			OpenConnect(server);
		} else {
			OpenConnect(szServer);
		}
		
	} else if (acReadBuffer[0] != '/') {

		// Submit chat

		if (clientStatus > 1) {
			char sendData[kBufferSize];
			snprintf(sendData, sizeof(sendData), "<%s> %s", szNickName, acReadBuffer);
	
			dh.dtype = 'C';
			dh.dsize = (short) strlen(sendData);
	
			SendCommand(sdex, &dh, sendData);
		} else {
			snprintf(pb, sizeof(pb), "I'm not connected to the arena");
			callUI(TUNNEL_EVENT_ERROR, 0, pb);
		}

	} else {
		snprintf(pb, sizeof(pb), "Command not supported : %s", acReadBuffer);
		callUI(TUNNEL_EVENT_ERROR, 0, pb);
	}
	return result;
}

/** 
 * Callback function to handle the packets captured in the WinPcap
 */
void packet_handler(u_char *, const struct pcap_pkthdr *header, const u_char *pkt_data)
{
	// I do not do anything not connected
	if (clientStatus < 2) {
		return;
	}
	char dummyPacket[16];
	ZeroMemory(dummyPacket, 16); 
	// I do not do anything if the dummy packet
	if (memcmp(dummyPacket, pkt_data, 12) == 0) {
		lastDummyPacket = clock();
		return;
	}
	int doSend = -1;
	for (int i = 0; i < maxMacAddr; i++) {
//		if (mac[i].last_clock + 1000 > clock()) {
			if (memcmp((char *)&mac[i], &pkt_data[6], 6) == 0) {
//				char comment[512];
//				snprintf(comment, sizeof(comment), "きゃぷ %d", clock());
//				SendChat(&gsd, comment);
				doSend = i;
				mac[i].last_clock = clock();
				last_psp_packet = clock();
				mac[i].findCount++;
				if (!mac[i].active) {
//					//printf("PSP認識(%d): MACアドレス = %0X:%0X:%0X:%0X:%0X:%0X\n", i + 1, mac[i].addr[0], mac[i].addr[1], mac[i].addr[2], mac[i].addr[3], mac[i].addr[4], mac[i].addr[5]);
//					char buf[33];
//					ZeroMemory(buf, 33);
//					CopyMemory(buf, Ssid.Ssid, Ssid.SsidLength);
					char psp[512];
					if (GetMACSetting(&mac[i], psp, sizeof(psp))) {
//						if (strlen(room) > 0) {
//							snprintf(comment, sizeof(comment), "%s が、%s に入りました", psp, room);
							//snprintf(comment, sizeof(comment), "%s の PSP(%d) が、%s に入りました", szNickName, i + 1, room);
//							SendChat(&gsd, comment);
//						}
						mac[i].doSend = true;
					} else {
						mac[i].doSend = false;
					}
					mac[i].active = true;
					SendSSID(&gsd);
					//printf("%s の PSP(%d) が、%s に入りました。\n", szNickName, i + 1, room);
				}
				break;
			}
//		}
	}

	if ((int)pkt_data[12] == 136 && (int)pkt_data[13] == 200 && doSend == -1) {

		bool ignore = false;

		// If Type is 88 C8, automatically recognize up to 4
		for (int i = 0; i < maxiMacAddr; i++) {
//			if (ignoreMac[i].last_clock + 1000 > clock()) {
			if (ignoreMac[i].active) {
				if (memcmp((char *)&ignoreMac[i], &pkt_data[6], 6) == 0) {
//		printf("スルー: to %0X:%0X:%0X:%0X:%0X:%0X from %0X:%0X:%0X:%0X:%0X:%0X %d バイト\n", pkt_data[0], pkt_data[1], pkt_data[2], pkt_data[3], pkt_data[4], pkt_data[5], pkt_data[6], pkt_data[7], pkt_data[8], pkt_data[9], pkt_data[10], pkt_data[11], (int)header->len);
					ignore = true;
					ignoreMac[i].last_clock = clock();
					break;
				}
			}
		}
		
		if (!ignore) {
			for (int i = 0; i < maxMacAddr; i++) {
				if (mac[i].last_clock + 1000 < clock()) {
					memcpy((char *)&mac[i], &pkt_data[6], 6);
					mac[i].last_clock = clock();
					mac[i].findCount = 0;
					mac[i].active = false;
					mac[i].doSend = false;
					//doSend = i;
					//printf("PSP認識(%d): MACアドレス = %0X:%0X:%0X:%0X:%0X:%0X\n", i + 1, mac[i].addr[0], mac[i].addr[1], mac[i].addr[2], mac[i].addr[3], mac[i].addr[4], mac[i].addr[5]);
					break;
				}
			}
		}
	}

	if (doSend > -1) {
		// The source also subject to redirect, if there is no need to be redirected to the destination, and sends
		for (int i = 0; i < maxMacAddr; i++) {
			if (mac[i].last_clock > 0) {
				if (memcmp((char *)&mac[i], &pkt_data[0], 6) == 0) {
//					printf("同エリア内送信のためスルー : PSP(%d) -> PSP(%d)\n", doSend + 1, i + 1);
					doSend = -1;
					break;
				}
			}
		}
	}

	if (doSend > -1) {
		
		if (mac[doSend].doSend) {
			mac[doSend].last_clock = clock();
			DATA_HEADER dh;
			dh.dtype = 'T';
			dh.dsize = (short)header->len;
	
			do {
				if (SendCommand(&gsd, &dh, (char *)pkt_data)) {
	//				printf("送信(%d) %d バイト (S%d R%d)\n", doSend, header->len, gsd.sendCount, gsd.recvCount);
				} else {
	//				printf("切断したので再接続します。\n");
				    //gsd = EstablishConnection(&gsd, nRemoteAddress, htons(nPort));
	//			    if (!EstablishConnection(&gsd, nRemoteAddress, htons(nPort))) {
	//			        cerr << WSAGetLastErrorMessage("再接続失敗。") << 
	//			                endl;
	//				}
	//		        Sleep(100);
				}
			} while (gsd.state != STATE_CONNECTED);
		}
	} else {
//		printf("スルー: to %0X:%0X:%0X:%0X:%0X:%0X from %0X:%0X:%0X:%0X:%0X:%0X %d バイト\n", pkt_data[0], pkt_data[1], pkt_data[2], pkt_data[3], pkt_data[4], pkt_data[5], pkt_data[6], pkt_data[7], pkt_data[8], pkt_data[9], pkt_data[10], pkt_data[11], (int)header->len);
	}
}

pcap_if_t *alldevs;
pcap_if_t *d;

bool IsWirelessLan(char * name)
{
	LPADAPTER lpAdapter = PacketOpenAdapter(name);
	NDIS_PHYSICAL_MEDIUM media;
	bool result = false;
//	bool result = true;
	
	if ( pktGetGenPhysicalMedium(lpAdapter, &media) )
	{
//		snprintf(pb, sizeof(pb), "%d = %d", NdisPhysicalMediumWirelessLan, media);
//		callUI(TUNNEL_EVENT_ERROR, 0, pb);
//	  if ( NdisPhysicalMediumWirelessLan == media )
	  if ( NdisPhysicalMediumWirelessLan == media )
	  {
		result = true;
	  }
	}
	PacketCloseAdapter(lpAdapter);
	return result;
}

bool NextDevice(WIRELESS_LAN_DEVICE * dev)
{
	d=d->next;
	if (d == NULL) {
		return false;
	}
	dev->name = d->name;
	dev->description = (char *)malloc(128);
	if (!GetDeviceDescription(dev->name, dev->description, 128)) {
		strcpy(dev->description, d->description);
	}
	if (IsWirelessLan(dev->name)) {
		return true;
	}
//	callUI(TUNNEL_EVENT_ERROR, 0, "IsWirelessLan=FALSE");
//	return true;
	return NextDevice(dev);
}

bool FindDevice(WIRELESS_LAN_DEVICE * dev)
{
	if (alldevs != NULL) {
		pcap_freealldevs(alldevs);
	}
	if(pcap_findalldevs(&alldevs, errbuf) == -1)
	{
		callUI(TUNNEL_EVENT_ERROR, 0, "I failed to get the list of network devices");
		return false;
	}
	d=alldevs;
	dev->name = d->name;
	dev->description = (char *)malloc(128);
	if (!GetDeviceDescription(dev->name, dev->description, 128)) {
		strcpy(dev->description, d->description);
	}
	if (IsWirelessLan(dev->name)) {
		return true;
	}
//	callUI(TUNNEL_EVENT_ERROR, 0, "IsWirelessLan=FALSE");
	return NextDevice(dev);
}

WIRELESS_LAN_DEVICE * FindDevices()
{
	WIRELESS_LAN_DEVICE * dev = (WIRELESS_LAN_DEVICE *)malloc(sizeof(WIRELESS_LAN_DEVICE));
	WIRELESS_LAN_DEVICE * result = NULL;
	dev->nextDevice = NULL;
	if (FindDevice(dev))
	{
		result = dev;
		WIRELESS_LAN_DEVICE * ndev = (WIRELESS_LAN_DEVICE *)malloc(sizeof(WIRELESS_LAN_DEVICE));
		ndev->nextDevice = NULL;
		while (NextDevice(ndev))
		{
			dev->nextDevice = ndev;
			dev = ndev;
			ndev = (WIRELESS_LAN_DEVICE *)malloc(sizeof(WIRELESS_LAN_DEVICE));
		}
		free(ndev);
	}
	return result;
}

void FreeDevices(WIRELESS_LAN_DEVICE * dev)
{
	WIRELESS_LAN_DEVICE * thedev = dev;
	while (thedev != NULL)
	{
		WIRELESS_LAN_DEVICE * nextdev = thedev->nextDevice;
		free(thedev->description);
		free(thedev);
		thedev = nextdev;
	}
}

bool InitClient()
{
	last_psp_packet -= roomWait;
	InitializeCriticalSection(&pcapSection);
	InitializeCriticalSection(&commandSection);
	InitializeCriticalSection(&clientLogSection);
	LoadClientSetting();
	//SetServerLogHandler(ServerLogHandler);
//	command_result = (COMMAND_RESULT *)malloc(sizeof(COMMAND_RESULT));
//	command_result->next = NULL;
//	command_result->text = NULL;
	console_log = (CONSOLE_LOG_CHAIN *)malloc(sizeof(CONSOLE_LOG_CHAIN));
	console_log->next = NULL;
	console_log->log.text = NULL;
	hConsoleLogEvent = CreateEvent(NULL, true, false, "TUNNEL_CONSOLE");
	useEvent = false;
	ignoreMac[0].active = true;
	ZeroMemory(&ignoreMac[0].addr, 6);
	for (int i = 0; i < maxUsers; i++) {
		user_active[i] = false;
	}
	pinfo.hProcess = NULL;
	return InitTunnel();
}

bool CloseClient()
{
	CloseDevice();
	CloseConnect();
	DeleteCriticalSection(&commandSection);
	DeleteCriticalSection(&pcapSection);
	return CloseTunnel();
}

int __stdcall GetAdapterStatus()
{
	return adapterStatus;
}

int __stdcall GetClientStatus()
{
	return clientStatus;
}

BOOL WINAPI DllEntryPoint(HINSTANCE, DWORD fdwReason, LPVOID*)
{
	switch (fdwReason){
		case DLL_PROCESS_ATTACH: //Processing when loaded
//			LogOut("dll", "DLL_PROCESS_ATTACH");
			InitClient();
			return TRUE;
		case DLL_PROCESS_DETACH: //Processing when it is unloaded
			CloseDevice();
			CloseConnect();
			if (pinfo.hProcess != NULL) {
				TerminateProcess(pinfo.hProcess, 0);
			}
			//CloseServer();
			CloseClient();
//			LogOut("dll", "DLL_PROCESS_DETACH");
			return TRUE;
		case DLL_THREAD_ATTACH:
//			LogOut("dll", "DLL_THREAD_ATTACH");
			break;
		case DLL_THREAD_DETACH:
//			LogOut("dll", "DLL_THREAD_DETACH");
			break;
	}
	return TRUE;
}
