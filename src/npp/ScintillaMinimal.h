#pragma once

#include <windows.h>
#include <cstdint>

using Sci_Position = intptr_t;
using uptr_t = uintptr_t;
using sptr_t = intptr_t;

struct SCNotification
{
    NMHDR nmhdr{};
    Sci_Position position{};
    int ch{};
    int modifiers{};
    int modificationType{};
    const char* text{};
    Sci_Position length{};
    Sci_Position linesAdded{};
    int message{};
    uptr_t wParam{};
    sptr_t lParam{};
    Sci_Position line{};
    int foldLevelNow{};
    int foldLevelPrev{};
    int margin{};
    int listType{};
    int x{};
    int y{};
    int token{};
    Sci_Position annotationLinesAdded{};
    int updated{};
    int listCompletionMethod{};
    int characterSource{};
};

constexpr UINT SCI_GETLENGTH = 2006;
constexpr UINT SCI_GETCURRENTPOS = 2008;
constexpr UINT SCI_GOTOLINE = 2024;
constexpr UINT SCI_INDICSETSTYLE = 2080;
constexpr UINT SCI_INDICSETFORE = 2082;
constexpr UINT SCI_GETLINEENDPOSITION = 2136;
constexpr UINT SCI_GETFIRSTVISIBLELINE = 2152;
constexpr UINT SCI_GETLINE = 2153;
constexpr UINT SCI_GETLINECOUNT = 2154;
constexpr UINT SCI_LINEFROMPOSITION = 2166;
constexpr UINT SCI_POSITIONFROMLINE = 2167;
constexpr UINT SCI_GETTEXT = 2182;
constexpr UINT SCI_VISIBLEFROMDOCLINE = 2220;
constexpr UINT SCI_DOCLINEFROMVISIBLE = 2221;
constexpr UINT SCI_ENSUREVISIBLE = 2232;
constexpr UINT SCI_LINELENGTH = 2350;
constexpr UINT SCI_LINESONSCREEN = 2370;
constexpr UINT SCI_SETINDICATORCURRENT = 2500;
constexpr UINT SCI_INDICATORFILLRANGE = 2504;
constexpr UINT SCI_INDICATORCLEARRANGE = 2505;

constexpr int INDIC_TEXTFORE = 17;

constexpr UINT SCN_UPDATEUI = 2007;
constexpr UINT SCN_MODIFIED = 2008;

constexpr int SC_MOD_INSERTTEXT = 0x1;
constexpr int SC_MOD_DELETETEXT = 0x2;
constexpr int SC_PERFORMED_UNDO = 0x20;
constexpr int SC_PERFORMED_REDO = 0x40;

constexpr int SC_UPDATE_CONTENT = 0x1;
constexpr int SC_UPDATE_SELECTION = 0x2;
constexpr int SC_UPDATE_V_SCROLL = 0x4;
constexpr int SC_UPDATE_H_SCROLL = 0x8;
