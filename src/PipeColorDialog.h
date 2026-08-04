#pragma once

#include "PipeColorSettings.h"

#include <windows.h>

namespace NppGrandFantasia
{
    class PipeColorDialog
    {
    public:
        static bool Show(
            HINSTANCE instance,
            HWND owner,
            HWND notepadHandle,
            PipeColorSettings& settings);

    private:
        struct Context
        {
            PipeColorSettings original;
            PipeColorSettings working;
            HWND notepadHandle = nullptr;
        };

        static INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
        static void Initialize(HWND dialog, Context& context);
        static void UpdateEnabledColors(HWND dialog, const Context& context);
        static void ChoosePipeColor(HWND dialog, Context& context, int colorIndex);
        static void ChooseValidIdColor(HWND dialog, Context& context);
        static void ChooseBrokenTextColor(HWND dialog, Context& context);
        static bool ShowColorPicker(HWND dialog, COLORREF& color);
        static void DrawColorButton(
            const DRAWITEMSTRUCT& draw,
            COLORREF color,
            const wchar_t* label,
            bool enabled);
    };
}
