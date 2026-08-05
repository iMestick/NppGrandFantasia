#pragma once

#include "MirrorLinkManager.h"
#include "npp/PluginInterface.h"

#include <windows.h>

#include <vector>

namespace NppGrandFantasia
{
    class MirrorLinkDialog
    {
    public:
        MirrorLinkDialog(HINSTANCE instance, NppData nppData, MirrorLinkManager& manager);

        bool Show(HWND owner);

    private:
        static INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
        INT_PTR HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        void Initialize();
        void ReloadPairs();
        void HandleCheckChanged(int index, bool checked);
        void SetItemCheck(int index, bool checked);
        void SetStatus(const std::wstring& text, bool error = false);
        void ApplyTheme();
        void ResizeColumns();
        void FitWindowToRows();

        HINSTANCE _instance = nullptr;
        NppData _nppData{};
        MirrorLinkManager& _manager;
        HWND _dialog = nullptr;
        HWND _list = nullptr;
        HWND _status = nullptr;
        bool _updatingChecks = false;
        bool _statusIsError = false;
        std::vector<MirrorPairCandidate> _pairs;
    };
}
