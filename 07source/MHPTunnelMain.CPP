//---------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

#include <stdio.h>
#include <string.h>
#include <imm.h>
#include <clipbrd.hpp>
#include <mshtml.h>
#include <richedit.h>
#include "MHPTunnelMain.h"
#include "MHPTunnelConfig.h"
#include "MHPTunnelConnect.h"
#include "include/tunnel-client.h"
#include "cTextCommand.h"
//---------------------------------------------------------------------
#define IDM_NEW    101
#define IDM_OPEN    102
#define IDM_SAVE    103
#define IDM_COPY    104
#define IDM_PASTE   105
#define IDM_DELETE  106
#define IDM_CUT     107
//---------------------------------------------------------------------
#pragma resource "*.dfm"
TMHPTunnelForm *MHPTunnelForm;
//---------------------------------------------------------------------------
// Thread log monitoring
DWORD WINAPI MonitorLog(void * _obj)
{
	TMHPTunnelForm * obj = (TMHPTunnelForm *)_obj;
	while (!obj->closed) {
		CONSOLE_LOG * log = GetConsoleLog();
		if (log != NULL) {
			if (log->text != NULL) {
				bool addChat = false;
				switch (log->type)
				{
					case TUNNEL_EVENT_ERROR:
						//obj->ConsoleBox->Lines->Append(log->text);
						addChat = true;
						break;
					case TUNNEL_EVENT_NOTICE:
						//obj->ConsoleBox->Lines->Append(log->text);
						addChat = true;
						break;
					case TUNNEL_EVENT_CLIENTLOG:
						//obj->ConsoleBox->Lines->Append(log->text);
						addChat = true;
						break;
					case TUNNEL_EVENT_ENTERUSER:
						obj->UpdateUserList();
						break;
					default:
						break;
				}
				HWND fw = GetForegroundWindow();

				if (addChat && fw != obj->Handle) {
					bool ignoreCall = obj->GetBoolSetting("IgnoreCall", false);
					//bool useBalloon = obj->GetBoolSetting("UseBalloon", true);
					bool useTaskTray = obj->GetBoolSetting("UseTaskTray", true);
					AnsiString str;
					str = log->text;
					if (str.Pos("<call>") > 0 && !ignoreCall) {
						SetForegroundWindow(obj->Handle);
						MessageBox(obj->Handle, log->text, "MHP Tunnel", MB_OK | MB_ICONEXCLAMATION);
						obj->CommandBox->SetFocus();
					} else {
//						if (useBalloon) {
							if (useTaskTray) {
								obj->TrayIcon->Visible = true;
								obj->TrayIcon->BalloonHint = "";
								obj->TrayIcon->ShowBalloonHint();
								obj->TrayIcon->BalloonHint = log->text;
								obj->TrayIcon->BalloonTitle = "MHP Tunnel";
								obj->TrayIcon->ShowBalloonHint();
//								MessageBox(obj->Handle, log->text, "ShowBallonHint", MB_OK | MB_ICONEXCLAMATION);
//							} else {
//								TFlashWInfo fi;
//								fi.hwnd = obj->Handle;
//								fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
//								fi.uCount = 10;    //Number of flashes
//								fi.dwTimeout = 500;   //Interval of blinking
//								FlashWindowEx(&fi);
//	  							MessageBox(obj->Handle, log->text, "TFlashWInfo", MB_OK | MB_ICONEXCLAMATION);
//							}
						}
					}
				}

			}
		}


	};
	return 0;
}

void TMHPTunnelForm::clearConsole()
{
	AnsiString Str;
	SAFEARRAY *safe_array;
	VARIANT *variant;
	IHTMLDocument2 *Html;
	int eve;

	this->ConsoleBox->Align = alClient;
	this->RichConsole->Align = alClient;
	this->WebBrowser->Align = alClient;

	switch (this->ConsoleType) {
	case CONSOLE_MEMO:
		this->ConsoleBox->Visible = true;
		this->RichConsole->Visible = false;
		this->WebBrowser->Visible = false;
		this->ConsoleBox->Clear();
		break;
	case CONSOLE_RICH:
		this->ConsoleBox->Visible = false;
		this->RichConsole->Visible = true;
		this->WebBrowser->Visible = false;
		this->RichConsole->Clear();
		eve = SendMessage(this->RichConsole->Handle, EM_GETEVENTMASK, (WPARAM)0, 0);
		SendMessage(this->RichConsole->Handle, EM_SETEVENTMASK, (WPARAM)0, ENM_LINK | eve);
		SendMessage(this->RichConsole->Handle, EM_AUTOURLDETECT, (WPARAM)TRUE, 0);
		break;
	case CONSOLE_WEB:
		this->ConsoleBox->Visible = false;
		this->RichConsole->Visible = false;
		this->WebBrowser->Visible = true;
		Str="<html><head><style>body { font-size:9pt; margin:3px; border:solid 1px gray; font-family: \"MS UI Gothic\" }</style></head><body>";
		safe_array = SafeArrayCreateVector(VT_VARIANT,0,1);
		Html=NULL;
		SafeArrayAccessData(safe_array,(void **)&variant);
		variant->vt      = VT_BSTR;
		variant->bstrVal = SysAllocString(WideString(Str).c_bstr());
		SafeArrayUnaccessData(safe_array);
		this->WebBrowser->Navigate(L"about:blank");
		this->WebBrowser->Document->QueryInterface(IID_IHTMLDocument2,(void **)&Html);
		Html->write(safe_array);
		SafeArrayDestroy(safe_array);
		break;
	default:
		break;
	}
}
void TMHPTunnelForm::addLine(const char * color, const char * text)
{
	AnsiString Str;
	SAFEARRAY *safe_array;
	VARIANT *variant;
	IHTMLDocument2 *Html;
	int i;
	SETTEXTEX stex;

	switch (this->ConsoleType) {
	case CONSOLE_MEMO:
		this->ConsoleBox->Lines->Append(text);
		break;
	case CONSOLE_RICH:
		Str = Str.sprintf("cl%s", color);
		//(TColor)RGB(r, g, b);
		stex.flags = ST_DEFAULT;
		stex.codepage = CP_ACP;
		SendMessage(this->RichConsole->Handle, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
		this->RichConsole->SelAttributes->Color = StringToColor(Str);
		SendMessage(this->RichConsole->Handle, EM_REPLACESEL, (WPARAM)false, (LPARAM)text);
		SendMessage(this->RichConsole->Handle, EM_SCROLLCARET, (WPARAM)0, (LPARAM)0);
		SendMessage(this->RichConsole->Handle, EM_REPLACESEL, (WPARAM)false, (LPARAM)"\n");
		break;
	case CONSOLE_WEB:
		if (this->WebBrowser->Document == NULL) {
			return;
		}
		Str = Str.sprintf("<font color=%s>%s</font><br>\n", color, text);
		safe_array = SafeArrayCreateVector(VT_VARIANT, 0, 1);
		SafeArrayAccessData(safe_array,(void **)&variant);
		variant->vt      = VT_BSTR;
		variant->bstrVal = SysAllocString(WideString(Str).c_bstr());
		SafeArrayUnaccessData(safe_array);
		Html=NULL;
		this->WebBrowser->Document->QueryInterface(IID_IHTMLDocument2,(void **)&Html);
		Html->write(safe_array);
		SafeArrayDestroy(safe_array);
		Sleep(10);
		OleVariant(this->WebBrowser->Document).OlePropertyGet("body").OlePropertyGet("scrollHeight");
		OleVariant(this->WebBrowser->Document).OlePropertyGet("parentWindow").OleFunction("scroll", 0, i);
		break;
	default:
		break;
	}
}
//---------------------------------------------------------------------------
//Thread log monitoring
void ClientLogHandler(int eventType, int option, const char * _logText)
{
	char sender[64];
	const char * logText = _logText;
	char tmp[512];

	snprintf(sender, sizeof(sender), "MHP Tunnel");
	snprintf(tmp, sizeof(tmp), "%s", _logText);

	if (eventType == TUNNEL_EVENT_CLIENTLOG && logText[0] == '<') {
		logText = strchr(logText, '>');
		if (logText != NULL) {
			ZeroMemory(sender, sizeof(sender));
			memcpy(sender, _logText + 1, logText - _logText - 1);
			logText = logText + 2;
			switch (MHPTunnelForm->ConsoleType) {
			case TMHPTunnelForm::CONSOLE_WEB:
				snprintf(tmp, sizeof(tmp), "&lt;%s&gt; %s", sender, logText);
				break;
			default:
				snprintf(tmp, sizeof(tmp), "<%s> %s", sender, logText);
				break;
			}
		} else {
			logText = _logText;
		}
	}

	TMHPTunnelForm * obj = MHPTunnelForm;
	if (!obj->closed) {
		if (logText != NULL) {
			bool addChat = false;
			switch (eventType)
			{
				case TUNNEL_EVENT_ERROR:
					obj->addLine("Red", logText);
					//obj->ConsoleBox->Lines->Append(logText);
					addChat = true;
					break;
				case TUNNEL_EVENT_NOTICE:
					obj->addLine("Gray", logText);
					//obj->ConsoleBox->Lines->Append(logText);
					addChat = true;
					break;
				case TUNNEL_EVENT_CLIENTLOG:
					obj->addLine("Black", tmp);
					//obj->ConsoleBox->Lines->Append(logText);
					addChat = true;
					break;
				case TUNNEL_EVENT_ENTERUSER:
					obj->UpdateUserList();
					break;
				default:
					break;
			}

			HWND fw = GetForegroundWindow();
			bool ignoreCall = obj->GetBoolSetting("IgnoreCall", false);
			bool useTaskTray = obj->GetBoolSetting("UseTaskTray", true);
			AnsiString str = logText;
			if (addChat && fw != obj->Handle) {
				if (str.Pos("<call>") > 0 && !ignoreCall) {
					SetForegroundWindow(obj->Handle);
					MessageBox(obj->Handle, logText, sender, MB_OK | MB_ICONEXCLAMATION);
					obj->CommandBox->SetFocus();
				} else {
					if (useTaskTray) {
						obj->TrayIcon->Visible = true;
						obj->TrayIcon->BalloonHint = "";
						obj->TrayIcon->ShowBalloonHint();
						obj->TrayIcon->BalloonHint = logText;
						obj->TrayIcon->BalloonTitle = sender;
						obj->TrayIcon->ShowBalloonHint();
					}
				}
			} else {
				if (useTaskTray) {
					obj->TrayIcon->Visible = true;
					obj->TrayIcon->BalloonHint = "";
					obj->TrayIcon->ShowBalloonHint();
				}
			}
		}
	}
}
//---------------------------------------------------------------------
__fastcall TMHPTunnelForm::TMHPTunnelForm(TComponent *AOwner)
	: TForm(AOwner)
{
	Application->OnDeactivate = ApplicationDeactivate;
}
//---------------------------------------------------------------------

void __fastcall TMHPTunnelForm::MenuQuitClick(TObject *Sender)
{
	this->Close();
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::FormResize(TObject *Sender)
{
//	int consoleHeight = this->ClientHeight - this->CommandBox->Height - this->StatusBar->Height;
//	int consoleWidth = this->ClientWidth - this->ListUsers->Width - this->Splitter1->Width;
//	//this->ConsoleBox->Width = this->ClientWidth;
//	this->ConsoleBox->Width = consoleWidth;
//	this->ConsoleBox->Height = consoleHeight;
//	this->CommandBox->Top = consoleHeight;
//	this->CommandBox->Width = this->ClientWidth - this->ButtonSend->Width;
//	this->ButtonSend->Left = this->ClientWidth - this->ButtonSend->Width;
//	this->ButtonSend->Top = consoleHeight;
//	this->ListUsers->Left = consoleWidth + this->Splitter1->Width;
//	this->ListUsers->Height = consoleHeight;
//	this->Splitter1->Left = consoleWidth;
//	this->Splitter1->Height = consoleHeight;
}
//---------------------------------------------------------------------------
void __fastcall TMHPTunnelForm::FormCreate(TObject *Sender)
{
	ConsoleType = CONSOLE_RICH;

	this->clearConsole();

	cTextCommand cmd;
	cmd.execute("/version");
	char tmp[100];
	snprintf(tmp, sizeof(tmp), "MHP Tunnel GUI Client Ver%s by Pensil", cmd.nextResult());

//	wchar_t tmpw[200];
//	MultiByteToWideChar(CP_ACP, 0, tmp, strlen(tmp)+1, tmpw, sizeof(tmpw));

	this->addLine("Gray", tmp);
	this->addLine("Gray", "");
//	this->ConsoleBox->Text = tmp;
//	this->ConsoleBox->Lines->Append("");
//	this->ConsoleBox->Lines->Append("");

	UseConsoleEvent(true);

	SetClientLogHandler(ClientLogHandler);

	this->closed = false;
//	hMonitor = CreateThread(0, 0, MonitorLog, (DWORD *)this, 0, NULL);

	const char * nickName = cmd.getSetting("NickName");
	if (strlen(nickName) > 0) {
		if (strcmp(cmd.getSetting("AutoDevice"), "false")!=0) {
			TextCommand("/opendevice");
		}

		if (strcmp(cmd.getSetting("AutoConnect"), "false")!=0) {
			TextCommand("/connect");
		}
	}
	this->TrayIcon->Visible = this->GetBoolSetting("UseTaskTray", true);

	for (int i = 0; i <= historyMax; i++) {
		history[i] = NULL;
	}
	historyIndex = -1;
	historyTemp = NULL;

	this->ListUsers->Clear();
	this->ListPSP->Clear();
	//this->CommandBox-> = this->CommandBoxPaste;

}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::ButtonSendClick(TObject *Sender)
{
	AddHistory(this->CommandBox->Text.c_str());
	historyIndex = -1;

	cTextCommand cmd;
	cmd.execute(this->CommandBox->Text.c_str());
	const char * result;
	while ((result = cmd.nextResult()) != NULL) {
		//this->ConsoleBox->Lines->Append(result);
		this->addLine("blue", result);
	}
	this->CommandBox->Text = "";
	this->CommandBox->SetFocus();
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::MenuVersionClick(TObject *Sender)
{
	MessageBox(this->Handle, "MHPTunnel\nhttp://www.pensil.jp/MHPTunnel/", "Version information", MB_OK);
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::MenuClearConsoleClick(TObject *Sender)
{
	AnsiString Str="<html><head><style>body { font-size:9pt; margin:3px; border:solid 1px gray; font-family: \"MS UI Gothic\" }</style></head><body>";
	SAFEARRAY *safe_array = SafeArrayCreateVector(VT_VARIANT,0,1);
	VARIANT *variant;
	SafeArrayAccessData(safe_array,(void **)&variant);
	variant->vt      = VT_BSTR;
	variant->bstrVal = SysAllocString(WideString(Str).c_bstr());
	SafeArrayUnaccessData(safe_array);
	IHTMLDocument2  *Html=NULL;
	this->WebBrowser->Navigate(L"about:blank");
	this->WebBrowser->Document->QueryInterface(IID_IHTMLDocument2,(void **)&Html);
	Html->write(safe_array);
	SafeArrayDestroy(safe_array);
	//this->ConsoleBox->Text = "";
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::MenuDisconnectClick(TObject *Sender)
{
	TextCommand("/close");
	TextCommand("/closeserver");
	this->UpdateUserList();
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::MenuSettingUserClick(TObject *Sender)
{
	ConfigForm->ShowModal();
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::MenuConnectArenaClick(TObject *Sender)
{
	cTextCommand cmd;
	const char * nickName = cmd.getSetting("NickName");
	if (strlen(nickName) == 0) {
		MessageBox(this->Handle, "Please set your nickname", "MHP Tunnel", MB_OK);
		ConfigForm->ShowModal();
	} else {
		ConnectForm->ShowModal();
	}
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::FormClose(TObject *Sender)
{
	this->closed = true;
	this->TrayIcon->Visible = false;
	TextCommand("/close");
	TextCommand("/closeserver");
	WaitForSingleObject(this->hMonitor, INFINITE);
}
//---------------------------------------------------------------------------


void __fastcall TMHPTunnelForm::Button1Click(TObject *Sender)
{
	//this->ListUsers->AddItem("Pensil", (TObject*)"");
//	TListItem * item = this->ListUsers->TopItem;
//	item->SubItems->Add("500");
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::Button2Click(TObject *Sender)
{
  AnsiString Str="<marquee>aaaa</marquee>";

  SAFEARRAY *safe_array = SafeArrayCreateVector(VT_VARIANT,0,1);
  VARIANT *variant;

  SafeArrayAccessData(safe_array,(void **)&variant);
  variant->vt      = VT_BSTR;
  variant->bstrVal = SysAllocString(WideString(Str).c_bstr());
  SafeArrayUnaccessData(safe_array);

  IHTMLDocument2 *Html=NULL;
//  this->WebBrowser->Navigate(L"about:blank");
  this->WebBrowser->Document->QueryInterface(IID_IHTMLDocument2,(void **)&Html);
  Html->write(safe_array);

//  Html->close();
//  Html->Release();
//	this->TrayIcon->ShowBalloonHint();
}
//---------------------------------------------------------------------------

const char * TMHPTunnelForm::GetSetting(const char * key)
{
	cTextCommand cmd;
	return cmd.getSetting(key);
}

bool TMHPTunnelForm::GetBoolSetting(const char * key, bool def)
{
	cTextCommand cmd;
	const char * result = cmd.getSetting(key);
	if (def) {
		if (strcmp(result, "false") == 0) {
			return false;
		} else {
			return true;
		}
	} else {
		if (strcmp(result, "true") == 0) {
			return true;
		} else {
			return false;
		}
	}
}

void TMHPTunnelForm::SetSetting(const char * key, const char * value)
{
	cTextCommand cmd;
	cmd.setSetting(key, value);
}

void TMHPTunnelForm::SetBoolSetting(const char * key, bool value)
{
	cTextCommand cmd;
	if (value) {
		cmd.setSetting(key, "true");
	} else {
		cmd.setSetting(key, "false");
	}
}


void __fastcall TMHPTunnelForm::TrayIconClick(TObject *Sender)
{
	//	MessageBox(NULL, "Please set your nickname", "MHP Tunnel", MB_OK);
//	ShowWindow(Application->Handle, SW_SHOW);
//	SetForegroundWindow(this->Handle);
//	AnimateWindow(this->Handle, 200, AW_ACTIVATE);
	//if(this->WindowState == wsMinimized) {
//		this->WindowState == wsNormal;
 //		Application->RestoreTopMosts();
	//}
	//MHPTunnelForm->SetFocus();//->SetFocus();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Events when the application becomes inactive
void __fastcall TMHPTunnelForm::ApplicationDeactivate(TObject *Sender)
{
  //  Minimize application
  //Application->Minimize();
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::CommandBoxKeyDown(TObject *Sender, WORD &Key,
	  TShiftState Shift)
{
	if (isIMEInputNow(this->CommandBox->Handle)) {
		return;
	}
	// Key = �� 38 �� 40
	if (Key == 40) {
		if (historyIndex != -1) {
			bool useHistory = false;
			if (historyIndex + 1 <= historyMax) {
				if (history[historyIndex + 1] != NULL) {
					historyIndex++;
					this->CommandBox->Text = history[historyIndex];
					useHistory = true;
				}
			}
			if (!useHistory) {
				 this->CommandBox->Text = historyTemp;
				 historyIndex = -1;
			}
		}
		this->CommandBox->SelStart = 100;
	}
}
//---------------------------------------------------------------------------

void TMHPTunnelForm::AddHistory(const char * command)
{
	if (command == NULL) {
		return;
	}
	int findIndex = -1;
	bool added = false;
	for (int i = 0; i <= historyMax; i++) {
		if (history[i] == NULL) {
			history[i] = (char *)malloc(strlen(command)+1);
			strcpy(history[i], command);
			added = true;
			break;
		} else if (strcmp(history[i], command)==0) {
			findIndex = i;
			break;
		}
	}
	if (findIndex > -1) {
		for (int i = findIndex; i < historyMax - 1; i++) {
			char * tmp = history[i];
			if (history[i+1] != NULL) {
				history[i] = history[i+1];
				history[i+1] = tmp;
			}
		}
	} else {
		if (!added) {
			free(history[0]);
			for (int i = 0; i < historyMax - 1; i++) {
				history[i] = history[i+1];
			}
			history[historyMax-1] = (char *)malloc(strlen(command)+1);
			strcpy(history[historyMax-1], command);
		}
	}
	historyIndex = -1;
}
void __fastcall TMHPTunnelForm::CommandBoxKeyUp(TObject *Sender, WORD &Key,
	  TShiftState Shift)
{
	if (isIMEInputNow(this->CommandBox->Handle)) {
		return;
	}
	// Key = �� 38 �� 40
	if (Key == 38) {
		if (historyIndex == -1) {
			if (historyTemp != NULL) {
				free(historyTemp);
			}
			historyTemp = (char *)malloc(strlen(this->CommandBox->Text.c_str())+1);
			strcpy(historyTemp, this->CommandBox->Text.c_str());
			for (int i = historyMax - 1; i >= 0; i--) {
				if (history[i] != NULL) {
					 this->CommandBox->Text = history[i];
					 historyIndex = i;
					 break;
				}
			}
		} else {
			for (int i = historyIndex - 1; i >= 0; i--) {
				if (history[i] != NULL) {
					 this->CommandBox->Text = history[i];
					 historyIndex = i;
					 break;
				}
			}
		}
		this->CommandBox->SelStart = 100;
	}
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::CommandBoxKeyPress(TObject *Sender, char &Key)
{
//	char tmp[40];
//	snprintf(tmp, sizeof(tmp), "CommandBoxKeyPress %d", Key);
//	MessageBox(this->Handle, tmp, "CommandBoxKeyPress", MB_OK);
}
//---------------------------------------------------------------------------

bool TMHPTunnelForm::isIMEInputNow(HWND hWnd)
{
	HIMC hImc = ImmGetContext(hWnd);
	if (hImc != NULL) {
		CANDIDATEFORM cform;
		if (ImmGetCandidateWindow(hImc, sizeof(cform), &cform)) {
			if (cform.dwStyle == CFS_CANDIDATEPOS) {
				return true;
			}
		}
		if (ImmGetCompositionString(hImc, GCS_COMPSTR, NULL, 0) > 0) {
			return true;
		}
	}
	return false;
}

TListItem * TMHPTunnelForm::FindItem(const char * uid)
{
	if (uid == NULL) {
		return NULL;
	}
	if (this->ListUsers == NULL) {
		return NULL;
	}
	if (this->ListUsers->Items == NULL) {
		return NULL;
	}
	TListItem * item;
	for (int i = 0; i < this->ListUsers->Items->Count; i++) {
		item = this->ListUsers->Items->Item[i];
		if (item == NULL) {
			continue;
		}
		if (item->Data == NULL) {
			continue;
		}
		if (strcmp((char *)(item->Data), uid) == 0) {
			return item;
		}
	}
	return NULL;
}

TListItem * TMHPTunnelForm::FindItemPSP(const char * uid)
{
	if (uid == NULL) {
		return NULL;
	}
	if (this->ListPSP == NULL) {
		return NULL;
	}
	if (this->ListPSP->Items == NULL) {
		return NULL;
	}
	TListItem * item;
	for (int i = 0; i < this->ListPSP->Items->Count; i++) {
		item = this->ListPSP->Items->Item[i];
		if (item == NULL) {
			continue;
		}
		if (item->Data == NULL) {
			continue;
		}
		if (strcmp((char *)(item->Data), uid) == 0) {
			return item;
		}
	}
	return NULL;
}

void TMHPTunnelForm::UpdateUserList()
{
	if (GetClientStatus() < 2) {
		this->ListUsers->Clear();
		this->ListPSP->Clear();
		return;
	}
	if (this->closed) {
		return;
	}
	cTextCommand userIdc;
	userIdc.execute("/u");
	int c = 0;
	char * userId[100];
	int pspNum = 0;
	char * userPSP[200];
	while (c < 100) {
		const char * tmp = userIdc.nextResult();
		if (tmp != NULL) {
			userId[c] = (char *)malloc(strlen(tmp) + 1);
			strcpy(userId[c], tmp);
		} else {
			break;
		}
		c++;
	}
	for (int i = 0; i < c; i++) {

//		const char * id = userId.get(i);
		const char * id = userId[i];
		char tmp[200];
		snprintf(tmp ,sizeof(tmp), "/userinfo %s", id);
		cTextCommand cmd;
		cmd.execute(tmp);
		if (cmd.size() < 8) {
			continue;
		}
		//const char * uid = cmd.get(0);//uid
		//const char * pid = cmd.get(1);//pid
		//const char * ver = cmd.get(2);//ver
		const char * name = cmd.get(3);//name
		//const char * ssid = cmd.get(4);//ssid
		const char * room = cmd.get(5);//room
		const char * ping = cmd.get(6);//ping
		const char * pspc = cmd.get(7);//pspCount

		TListItem * item = this->FindItem(id);
		if (item == NULL) {
			char * newId = (char *) malloc(strlen(id) + 1);
			strcpy(newId, id);
			this->ListUsers->AddItem(name, (TObject *)newId);
			item = this->FindItem(id);
			item->Data = newId;
			item->SubItems->Add(ping);
		}
		if (strcmp(item->Caption.c_str(),name) != 0) {
			item->Caption = name;
		}
		if (strcmp(item->SubItems->Strings[0].c_str(),ping) != 0) {
			item->SubItems->Strings[0] = ping;
		}

		int pspCount = 0;
		sscanf(pspc, "%d", &pspCount);
		for (int p = 0; p < pspCount; p++) {
			snprintf(tmp ,sizeof(tmp), "%s:%d", id, p);
			userPSP[pspNum] = (char *)malloc(strlen(tmp)+1);
			strcpy(userPSP[pspNum], tmp);
			TListItem * itemPSP = this->FindItemPSP(tmp);
			if (itemPSP == NULL) {
				char * newId = (char *) malloc(strlen(tmp) + 1);
				strcpy(newId, tmp);
				this->ListPSP->AddItem("", (TObject *)newId);
				itemPSP = this->FindItemPSP(newId);
				itemPSP->ImageIndex = 1;
				itemPSP->Data = newId;
				itemPSP->SubItems->Add(room);
			}
			if (p == 0) {
				itemPSP->Caption = name;
			} else {
				snprintf(tmp ,sizeof(tmp), "%s(%d)", name, p+1);
				itemPSP->Caption = tmp;
			}
			if (strcmp(itemPSP->SubItems->Strings[0].c_str(),room) != 0) {
				itemPSP->SubItems->Strings[0] = room;
			}
//			itemPSP->SubItems->Text = room;
			pspNum++;
		}
	}
	if (this->ListUsers == NULL) {
		return;
	}
	if (this->ListUsers->Items == NULL) {
		return;
	}
	for (int i = this->ListUsers->Items->Count - 1; i >= 0; i--) {
		TListItem * item = this->ListUsers->Items->Item[i];
		if (item == NULL) {
			continue;
		}
		if (item->Data == NULL) {
//			this->ListUsers->Items->Delete(i);
			item->Delete();
			continue;
		}
		bool find = false;
		for (int j = 0; j < c; j++) {
			if (strcmp((char *)(item->Data), userId[j]) == 0) {
				find = true;
				break;
			}
		}
		if (!find) {
			free(item->Data);
			item->Data = NULL;
			item->Delete();
		}
	}
	for (int j = 0; j < c; j++) {
		free(userId[j]);
	}
	if (this->ListPSP == NULL) {
		return;
	}
	if (this->ListPSP->Items == NULL) {
		return;
	}
	for (int i = this->ListPSP->Items->Count - 1; i >= 0; i--) {
		TListItem * item = this->ListPSP->Items->Item[i];
		if (item == NULL) {
			continue;
		}
		if (item->Data == NULL) {
			//this->ListPSP->Items->Delete(i);
			item->Delete();
			//update = true;
			break;
		}
		bool find = false;
		for (int j = 0; j < pspNum; j++) {
			if (strcmp((char *)(item->Data), userPSP[j]) == 0) {
				find = true;
				break;
			}
		}
		if (!find) {
			free(item->Data);
			item->Data = NULL;
			item->Delete();
			//update = true;
			break;
		}
	}
	for (int j = 0; j < pspNum; j++) {
		free(userPSP[j]);
	}
}
void __fastcall TMHPTunnelForm::ApplicationEventsMessage(tagMSG &Msg,
	  bool &Handled)
{
	Handled = false;
	if (Msg.hwnd == this->CommandBox->Handle) {
		if (Msg.message == WM_KEYDOWN && Msg.wParam != VK_SHIFT && Msg.wParam != VK_CONTROL) {

			if (::GetKeyState(VK_CONTROL) < 0 && Msg.wParam == 86) {
				//char tmp[50];
				//snprintf(tmp, sizeof(tmp), "%d,%lld", Msg.wParam, Msg.lParam);
				//MessageBox(this->Handle, tmp, "���[", MB_OK);
				this->CommandBoxPaste();
				Handled = true;
			}
			if (::GetKeyState(VK_SHIFT) < 0 && Msg.wParam == 45) {
				//char tmp[50];
				//snprintf(tmp, sizeof(tmp), "%d,%lld", Msg.wParam, Msg.lParam);
				//MessageBox(this->Handle, tmp, "���[", MB_OK);
				this->CommandBoxPaste();
				Handled = true;
			}

		}
	}
	//if (Msg.hwnd == this->Handle) {
		if (Msg.message == WM_NOTIFY) {

			//if (((NMHDR *)(Msg.lParam))->code == EN_LINK) {
//				char tmp[50];
//				snprintf(tmp, sizeof(tmp), "%d,%lld\n", Msg.wParam, Msg.lParam);
//				this->Memo1->Lines->Add(tmp);
				//MessageBox(this->Handle, tmp, "���[", MB_OK);
			//}
		}
	//}
}
//---------------------------------------------------------------------------


void __fastcall TMHPTunnelForm::MenuOnlineManualClick(TObject *Sender)
{
	// To the online manual
	ShellExecute(this->Handle,"open","http://www.pensil.jp/MHPTunnel/Readme.htm",0,0,1);
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::CommandBoxPaste()
{
	AnsiString aStr = Clipboard()->AsText;
	char * cstr = aStr.c_str();
	char * start = cstr;
	char * end;
	while ((end = strchr(start, '\n')) != NULL) {
		int size = end - start;
		char * moji = (char *)malloc(size + 1);
		memcpy(moji, start, size);
		moji[size-1] = 0;
		start = end + 1;
		this->CommandBox->SelText = moji;
		free(moji);
		this->ButtonSend->Click();
	}
	this->CommandBox->SelText = start;
}

void __fastcall TMHPTunnelForm::PopupCopyClick(TObject *Sender)
{
	this->CommandBox->CopyToClipboard();
}
//---------------------------------------------------------------------------


void __fastcall TMHPTunnelForm::PopupPasteClick(TObject *Sender)
{
	this->CommandBoxPaste();
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::ListUsersDblClick(TObject *Sender)
{
	if (this->ListUsers->Selected != NULL) {
		this->CommandBox->Text = AnsiString("/tell ") + this->ListUsers->Selected->Caption + " ";
		this->CommandBox->SetFocus();
		this->CommandBox->SelStart = this->CommandBox->Text.Length();
	}
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::PopupPasteHTMLClick(TObject *Sender)
{
	this->CommandBoxPaste();
}
//---------------------------------------------------------------------------

void __fastcall TMHPTunnelForm::WndProc(Messages::TMessage &Message)
{
	//char tmp[50];
	TWMNotify NotifyMsg;
	TENLink Enl;
	CHARRANGE tmp;
	char * url;
	int l;
	switch( Message.Msg ){
	case WM_NOTIFY:
		if (((NMHDR *)(Message.LParam))->code == EN_LINK) {
			memcpy(&Enl, (char *)(Message.LParam), sizeof(TENLink));

			if (Enl.msg == WM_LBUTTONUP) {
				l = Enl.chrg.cpMax - Enl.chrg.cpMin;
				url = (char *)malloc(l + 1);
				SendMessage(this->RichConsole->Handle, EM_EXGETSEL, 0, (LPARAM)&tmp);
				SendMessage(this->RichConsole->Handle, EM_EXSETSEL, 0, (LPARAM)&Enl.chrg);
				SendMessage(this->RichConsole->Handle, EM_GETSELTEXT, 0, (LPARAM)url);
				SendMessage(this->RichConsole->Handle, EM_EXSETSEL, 0, (LPARAM)&tmp);
//				snprintf(tmp, sizeof(tmp), "%d,%lld\n", Message.WParam, Message.LParam);
				ShellExecute(this->Handle,"open",url,0,0,1);
				//this->Memo1->Lines->Add(url);
				free(url);
			}
//				char tmp[50];
//				snprintf(tmp, sizeof(tmp), "%d,%lld\n", Msg.wParam, Msg.lParam);
//				this->Memo1->Lines->Add(tmp);
				//MessageBox(this->Handle, tmp, "���[", MB_OK);
//		TWMCopyData *msg = (TWMCopyData *)&Message;
//		MessageBox(0,(char *)msg->CopyDataStruct->lpData,"Oh!",MB_OK);
		}
		break;
	}
	TForm::WndProc(Message);
}

