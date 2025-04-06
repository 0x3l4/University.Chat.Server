#pragma once

#include "resource.h"
#include <vector>
#include <string>

#define MAX_LOADSTRING 100
#define BUFSIZE 512
#define WINDOW_X 800
#define WINDOW_Y 530
#define WM_PIPE_DATA (WM_USER + 1) // Ïîëüçîâàòåëüñêîå ñîîáùåíèå äëÿ ïåðåäà÷è äàííûõ èç ïîòîêà

enum {
	IDC_EDIT_MSG = 201,
	IDC_EDIT_INFO = 202,
	IDC_BTN_START = 203,
	IDC_BTN_STOP = 204,
	IDC_BTN_EXIT = 205
};

typedef struct
{
	OVERLAPPED oOverlap;
	HANDLE hPipeInst;
	TCHAR chRequest[BUFSIZE];
	DWORD cbRead;
	TCHAR chReply[BUFSIZE];
	DWORD cbToWrite;
	BOOL bExclusiveMode;
} PIPEINST, * LPPIPEINST;

// Ãëîáàëüíûå ïåðåìåííûå:
HINSTANCE hInst;                                // òåêóùèé ýêçåìïëÿð
WCHAR szTitle[MAX_LOADSTRING];                  // Òåêñò ñòðîêè çàãîëîâêà
WCHAR szWindowClass[MAX_LOADSTRING];            // èìÿ êëàññà ãëàâíîãî îêíà

//Ìîè ãëîáàëüíûå ïåðåìåííûå
HANDLE hServerThread = NULL,					// Äåñêðèïòîð ñåðâåðíîãî ïîòîêà
hPipeServer = INVALID_HANDLE_VALUE,				// Äåñêðèïòîð òåêóùåãî èìåíîâàííîãî êàíàëà
hPipe = NULL;
HWND hEditMsg = NULL, hEditInfo = NULL;			// Äåñêðèïòîðû òåêñòîâûõ ïîëåé
HWND hButtonClose, hButtonStart, hButtonExit;	// Êíîïî÷êè
HANDLE hMutex;									// Ìüþòåêñ

//Ôëàãè ñåðâåðà
volatile bool exclusiveActive = false;
volatile bool mutexRaised = false;
volatile bool isServerDown = true;

CRITICAL_SECTION cs;

// Îòïðàâèòü îáúÿâëåíèÿ ôóíêöèé, âêëþ÷åííûõ â ýòîò ìîäóëü êîäà:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

DWORD	WINAPI		ReceiverThread(LPVOID);
void	WINAPI		CompletedWriteRoutine(DWORD, DWORD, LPOVERLAPPED);
void	WINAPI		CompletedReadRoutine(DWORD, DWORD, LPOVERLAPPED);
void				DisconnectAndClose(LPPIPEINST);
void				GetAnswerToRequest(LPPIPEINST);
BOOL				CreateAndConnectInstance(LPOVERLAPPED);
BOOL				ConnectToNewClient(HANDLE, LPOVERLAPPED);

void				OnCreate(HWND);
void				OnStartServer(HWND);
void				OnStopServer();
void				LogStringCreator(const wchar_t*, const wchar_t*);
void				PrintMessage(const wchar_t*, HWND);
std::wstring		GetCurrentTimeString();