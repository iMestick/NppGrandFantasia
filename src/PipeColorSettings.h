#pragma once

#include "npp/PluginInterface.h"

#include <array>
#include <string>

namespace NppGrandFantasia
{
    struct PipeColorSettings
    {
        int colorCount = 4;
        std::array<COLORREF, 4> colors{
            RGB(255, 170, 0),
            RGB(75, 185, 255),
            RGB(215, 105, 255),
            RGB(95, 220, 145),
        };
        COLORREF brokenLineBackground = RGB(255, 70, 70);
    };

    PipeColorSettings DefaultPipeColorSettings();
    std::wstring GetSettingsFilePath(HWND notepadHandle);
    PipeColorSettings LoadPipeColorSettings(HWND notepadHandle);
    bool SavePipeColorSettings(HWND notepadHandle, const PipeColorSettings& settings);
}
