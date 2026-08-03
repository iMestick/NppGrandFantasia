#pragma once

#include <windows.h>

#define NPPMSG (WM_USER + 1000)

#define RUNCOMMAND_USER (WM_USER + 3000)
#define FILE_NAME 3
#define NPPM_GETFILENAME (RUNCOMMAND_USER + FILE_NAME)

#define NPPM_GETCURRENTSCINTILLA        (NPPMSG + 4)
#define NPPM_MODELESSDIALOG             (NPPMSG + 12)
#define NPPM_DMMSHOW                    (NPPMSG + 30)
#define NPPM_DMMHIDE                    (NPPMSG + 31)
#define NPPM_DMMUPDATEDISPINFO          (NPPMSG + 32)
#define NPPM_DMMREGASDCKDLG             (NPPMSG + 33)
#define NPPM_SETMENUITEMCHECK           (NPPMSG + 40)
#define NPPM_GETPLUGINSCONFIGDIR        (NPPMSG + 46)
#define NPPM_GETCURRENTBUFFERID         (NPPMSG + 60)
#define NPPM_ALLOCATEMARKER             (NPPMSG + 82)
#define NPPM_ISDARKMODEENABLED          (NPPMSG + 107)
#define NPPM_GETDARKMODECOLORS           (NPPMSG + 108)
#define NPPM_DARKMODESUBCLASSANDTHEME   (NPPMSG + 112)
#define NPPM_ALLOCATEINDICATOR          (NPPMSG + 113)

#define MODELESSDIALOGADD 0
#define MODELESSDIALOGREMOVE 1

namespace NppDarkMode
{
    struct Colors
    {
        COLORREF background = 0;
        COLORREF softerBackground = 0;
        COLORREF hotBackground = 0;
        COLORREF pureBackground = 0;
        COLORREF errorBackground = 0;
        COLORREF text = 0;
        COLORREF darkerText = 0;
        COLORREF disabledText = 0;
        COLORREF linkText = 0;
        COLORREF edge = 0;
        COLORREF hotEdge = 0;
        COLORREF disabledEdge = 0;
    };

    constexpr ULONG dmfInit = 0x0000000BUL;
    constexpr ULONG dmfHandleChange = 0x0000000CUL;
}

#define NPPN_FIRST 1000
#define NPPN_READY             (NPPN_FIRST + 1)
#define NPPN_FILEOPENED        (NPPN_FIRST + 4)
#define NPPN_FILESAVED         (NPPN_FIRST + 8)
#define NPPN_SHUTDOWN          (NPPN_FIRST + 9)
#define NPPN_BUFFERACTIVATED   (NPPN_FIRST + 10)
#define NPPN_LANGCHANGED       (NPPN_FIRST + 11)
#define NPPN_DARKMODECHANGED   (NPPN_FIRST + 27)
