#include "PipeColorSettings.h"

#include "npp/Notepad_plus_msgs.h"

#include <algorithm>
#include <cwchar>
#include <vector>

namespace NppGrandFantasia
{
    namespace
    {
        constexpr wchar_t SettingsSection[] = L"PipeColors";
        constexpr wchar_t SettingsFileName[] = L"NppGrandFantasia.ini";

        int ClampColorCount(int value)
        {
            return std::clamp(value, 1, 4);
        }
    }

    PipeColorSettings DefaultPipeColorSettings()
    {
        return PipeColorSettings{};
    }

    std::wstring GetSettingsFilePath(HWND notepadHandle)
    {
        const LRESULT required = SendMessageW(notepadHandle, NPPM_GETPLUGINSCONFIGDIR, 0, 0);
        if (required <= 0)
        {
            return SettingsFileName;
        }

        std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 2U, L'\0');
        if (!SendMessageW(
                notepadHandle,
                NPPM_GETPLUGINSCONFIGDIR,
                static_cast<WPARAM>(buffer.size()),
                reinterpret_cast<LPARAM>(buffer.data())))
        {
            return SettingsFileName;
        }

        std::wstring path(buffer.data());
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        {
            path.push_back(L'\\');
        }

        path += SettingsFileName;
        return path;
    }

    PipeColorSettings LoadPipeColorSettings(HWND notepadHandle)
    {
        PipeColorSettings settings = DefaultPipeColorSettings();
        const std::wstring path = GetSettingsFilePath(notepadHandle);

        settings.colorCount = ClampColorCount(GetPrivateProfileIntW(
            SettingsSection,
            L"Count",
            settings.colorCount,
            path.c_str()));

        for (std::size_t index = 0; index < settings.colors.size(); ++index)
        {
            const std::wstring key = L"Color" + std::to_wstring(index + 1U);
            const UINT fallback = static_cast<UINT>(settings.colors[index] & 0x00FFFFFFUL);
            settings.colors[index] = static_cast<COLORREF>(GetPrivateProfileIntW(
                SettingsSection,
                key.c_str(),
                static_cast<int>(fallback),
                path.c_str())) & 0x00FFFFFFUL;
        }

        const UINT brokenLineFallback =
            static_cast<UINT>(settings.brokenLineBackground & 0x00FFFFFFUL);
        settings.brokenLineBackground = static_cast<COLORREF>(GetPrivateProfileIntW(
            SettingsSection,
            L"BrokenLineBackground",
            static_cast<int>(brokenLineFallback),
            path.c_str())) & 0x00FFFFFFUL;

        return settings;
    }

    bool SavePipeColorSettings(HWND notepadHandle, const PipeColorSettings& source)
    {
        PipeColorSettings settings = source;
        settings.colorCount = ClampColorCount(settings.colorCount);
        const std::wstring path = GetSettingsFilePath(notepadHandle);

        bool success = true;
        const std::wstring count = std::to_wstring(settings.colorCount);
        success = WritePrivateProfileStringW(SettingsSection, L"Count", count.c_str(), path.c_str()) != FALSE && success;

        for (std::size_t index = 0; index < settings.colors.size(); ++index)
        {
            const std::wstring key = L"Color" + std::to_wstring(index + 1U);
            const std::wstring value = std::to_wstring(
                static_cast<unsigned long>(settings.colors[index] & 0x00FFFFFFUL));
            success = WritePrivateProfileStringW(
                SettingsSection,
                key.c_str(),
                value.c_str(),
                path.c_str()) != FALSE && success;
        }

        const std::wstring brokenLineValue = std::to_wstring(
            static_cast<unsigned long>(settings.brokenLineBackground & 0x00FFFFFFUL));
        success = WritePrivateProfileStringW(
            SettingsSection,
            L"BrokenLineBackground",
            brokenLineValue.c_str(),
            path.c_str()) != FALSE && success;

        return success;
    }
}
