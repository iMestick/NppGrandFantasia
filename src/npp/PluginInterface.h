#pragma once

#include <windows.h>
#include "ScintillaMinimal.h"
#include "Notepad_plus_msgs.h"

struct NppData
{
    HWND _nppHandle = nullptr;
    HWND _scintillaMainHandle = nullptr;
    HWND _scintillaSecondHandle = nullptr;
};

using PFUNCPLUGINCMD = void (__cdecl*)();

struct ShortcutKey
{
    bool _isCtrl = false;
    bool _isAlt = false;
    bool _isShift = false;
    UCHAR _key = 0;
};

constexpr int menuItemSize = 64;

struct FuncItem
{
    wchar_t _itemName[menuItemSize]{};
    PFUNCPLUGINCMD _pFunc = nullptr;
    int _cmdID = 0;
    bool _init2Check = false;
    ShortcutKey* _pShKey = nullptr;
};

extern "C" __declspec(dllexport) void setInfo(NppData notepadPlusData);
extern "C" __declspec(dllexport) const wchar_t* getName();
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* nbF);
extern "C" __declspec(dllexport) void beNotified(SCNotification* notifyCode);
extern "C" __declspec(dllexport) LRESULT messageProc(UINT message, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) BOOL isUnicode();
