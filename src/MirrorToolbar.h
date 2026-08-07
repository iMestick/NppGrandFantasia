#pragma once

#include <windows.h>

#include <string>

namespace NppGrandFantasia
{
    enum class MirrorToolbarState
    {
        Unlinked,
        Linked,
        Syncing,
        Error,
    };

    class MirrorToolbar
    {
    public:
        using LinkCallback = void (*)(void* context);
        using SyncCallback = void (*)(void* context);
        using FlagToolCallback = void (*)(void* context);

        MirrorToolbar() = default;
        ~MirrorToolbar();

        MirrorToolbar(const MirrorToolbar&) = delete;
        MirrorToolbar& operator=(const MirrorToolbar&) = delete;

        bool Create(
            HINSTANCE instance,
            HWND notepadHandle,
            LinkCallback linkCallback,
            SyncCallback syncCallback,
            FlagToolCallback flagToolCallback,
            void* callbackContext);
        void Destroy();
        void SetState(MirrorToolbarState state, std::wstring tooltipText = {});
        void ApplyTheme();
        void EnsureLayout();
        bool IsCreated() const;
        int ReservedWidthLogical() const;

    private:
        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK ToolbarSubclassProc(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam,
            UINT_PTR subclassId,
            DWORD_PTR referenceData);

        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        HWND FindNotepadToolbar() const;
        void Reposition();
        void Paint(HDC deviceContext, const RECT& clientRect);
        void UpdateTooltip();
        int Scale(int value) const;
        bool PointInSyncButton(POINT point) const;
        bool PointInFlagToolButton(POINT point) const;

        HINSTANCE _instance = nullptr;
        HWND _notepadHandle = nullptr;
        HWND _toolbar = nullptr;
        HWND _window = nullptr;
        HWND _tooltip = nullptr;
        LinkCallback _linkCallback = nullptr;
        SyncCallback _syncCallback = nullptr;
        FlagToolCallback _flagToolCallback = nullptr;
        void* _callbackContext = nullptr;
        MirrorToolbarState _state = MirrorToolbarState::Unlinked;
        std::wstring _tooltipText = L"Vincule S_ ao C_, abra o FlagTool ou sincronize os arquivos vinculados.";
        RECT _syncButton{};
        RECT _flagToolButton{};
        bool _hoverLink = false;
        bool _hoverSync = false;
        bool _hoverFlagTool = false;
        bool _mouseTracking = false;
    };
}
