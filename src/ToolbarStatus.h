#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

namespace NppGrandFantasia
{
    struct ToolbarErrorLink
    {
        std::size_t startLine = 0;
        std::size_t endLine = 0;
    };

    class ToolbarStatus
    {
    public:
        using NavigateCallback = void (*)(void* context, std::size_t oneBasedLine);
        using TogglePanelCallback = void (*)(void* context);

        ToolbarStatus() = default;
        ~ToolbarStatus();

        ToolbarStatus(const ToolbarStatus&) = delete;
        ToolbarStatus& operator=(const ToolbarStatus&) = delete;

        bool Create(
            HINSTANCE instance,
            HWND notepadHandle,
            NavigateCallback navigateCallback,
            TogglePanelCallback togglePanelCallback,
            void* callbackContext);
        void Destroy();

        // Mensagens gerais, usadas quando a validacao nao esta ativa ou esta em andamento.
        void SetStatus(std::wstring text, bool validationActive, bool hasError);

        // Exibe as linhas quebradas em ordem. Cada numero/faixa visivel e clicavel.
        void SetErrorLines(std::vector<ToolbarErrorLink> errorLines, bool validationActive);

        void SetErrorColor(COLORREF color);
        void ApplyTheme();
        bool IsCreated() const;

    private:
        struct ClickRegion
        {
            RECT bounds{};
            std::size_t oneBasedLine = 0;
            std::size_t errorIndex = 0;
        };

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
        void PaintErrorLines(
            HDC deviceContext,
            const RECT& textBounds,
            COLORREF textColor,
            COLORREF mutedTextColor);
        void UpdateTooltipText();
        void NavigateFromPoint(POINT point);
        int HitTestError(POINT point) const;
        void UpdateHover(POINT point);
        static std::wstring FormatLineRange(const ToolbarErrorLink& error);
        int Scale(int value) const;

        HINSTANCE _instance = nullptr;
        HWND _notepadHandle = nullptr;
        HWND _toolbar = nullptr;
        HWND _window = nullptr;
        HWND _tooltip = nullptr;
        NavigateCallback _navigateCallback = nullptr;
        TogglePanelCallback _togglePanelCallback = nullptr;
        void* _callbackContext = nullptr;
        std::wstring _text = L"Sem validacao";
        std::wstring _tooltipText;
        std::vector<ToolbarErrorLink> _errorLines;
        std::vector<ClickRegion> _clickRegions;
        bool _validationActive = false;
        bool _hasError = false;
        bool _mouseTracking = false;
        int _hoveredError = -1;
        COLORREF _errorColor = RGB(255, 70, 70);
    };
}
