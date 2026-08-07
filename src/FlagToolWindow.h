#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace NppGrandFantasia
{
    class FlagToolWindow
    {
    public:
        FlagToolWindow() = default;
        ~FlagToolWindow();

        FlagToolWindow(const FlagToolWindow&) = delete;
        FlagToolWindow& operator=(const FlagToolWindow&) = delete;

        bool Show(HINSTANCE instance, HWND ownerWindow);
        void Destroy();
        bool IsCreated() const;

    private:
        struct BigUInt
        {
            std::vector<std::uint32_t> limbs;

            bool IsZero() const;
            void Normalize();
            void Clear();
            void OrWith(const BigUInt& other);
            bool ContainsBits(const BigUInt& other) const;
            std::wstring ToDecimal() const;
            std::wstring ToHex() const;

            static bool ParseDecimal(const std::wstring& text, BigUInt& value);
            static bool ParseHex(const std::wstring& text, BigUInt& value);
        };

        struct OptionState
        {
            std::wstring name;
            BigUInt value;
            bool selected = false;
        };

        struct TabState
        {
            std::wstring displayName;
            bool hexadecimal = false;
            bool initialized = false;
            std::vector<OptionState> options;
            BigUInt total;
        };

        struct VisibleFlagButton
        {
            HWND window = nullptr;
            std::size_t optionIndex = 0;
            bool bold = false;
        };

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        bool Create(HINSTANCE instance, HWND ownerWindow);
        void InitializeFonts();
        void DestroyFonts();
        void InitializeTabs();
        void InitializeTab(std::size_t tabIndex);
        void CreatePersistentControls();
        void RebuildCurrentTab();
        void DestroyFlagButtons();
        void LayoutControls();
        void BuildDecimalGrid(const RECT& contentRect);
        void BuildClassGrid(const RECT& contentRect);
        void CreateFlagButton(std::size_t optionIndex, const RECT& rect, bool bold);
        void SwitchTab(std::size_t tabIndex);
        void ShowTabMenu();
        void ToggleOption(std::size_t optionIndex);
        void RecalculateCurrentTotal();
        void CheckCurrentInput();
        void MarkAllCurrent();
        void ClearAllCurrent();
        void CopyCurrentResult();
        void UpdateResultEdit();
        std::wstring CurrentResultText() const;
        std::wstring GetTrimmedEditText() const;
        void CenterOnOwner();

        void PaintBackground(HDC deviceContext, const RECT& clientRect);
        void DrawOwnerButton(const DRAWITEMSTRUCT& drawItem);
        void DrawFlagButton(const DRAWITEMSTRUCT& drawItem, const VisibleFlagButton& flagButton);
        void DrawActionButton(const DRAWITEMSTRUCT& drawItem, const wchar_t* text, bool active = false);
        void DrawTabSelector(const DRAWITEMSTRUCT& drawItem);
        void DrawCenteredWrappedText(HDC deviceContext, const RECT& rect, const std::wstring& text, HFONT font, COLORREF color);
        HFONT CellFontForText(const std::wstring& text, bool bold) const;
        HBRUSH HandleControlColor(HDC deviceContext, HWND control, UINT message);

        int Scale(int value) const;
        RECT ClientRect() const;
        RECT ContentRect() const;
        TabState& CurrentTab();
        const TabState& CurrentTab() const;
        const VisibleFlagButton* FindVisibleFlagButton(HWND window) const;
        static std::wstring Trim(const std::wstring& text);

        HINSTANCE _instance = nullptr;
        HWND _ownerWindow = nullptr;
        HWND _window = nullptr;
        HWND _toolLabel = nullptr;
        HWND _tabSelector = nullptr;
        HWND _resultEdit = nullptr;
        HWND _checkButton = nullptr;
        HWND _copyButton = nullptr;
        HWND _markAllButton = nullptr;
        HWND _clearAllButton = nullptr;
        HFONT _fontDefault = nullptr;
        HFONT _fontBold = nullptr;
        HFONT _fontEntry = nullptr;
        HFONT _fontCell10 = nullptr;
        HFONT _fontCell10Bold = nullptr;
        HFONT _fontCell9 = nullptr;
        HFONT _fontCell9Bold = nullptr;
        HFONT _fontCell8 = nullptr;
        HFONT _fontCell8Bold = nullptr;
        HBRUSH _backgroundBrush = nullptr;
        HBRUSH _headerBrush = nullptr;
        HBRUSH _cardBrush = nullptr;
        std::vector<TabState> _tabs;
        std::vector<VisibleFlagButton> _visibleFlagButtons;
        std::vector<RECT> _nullCellRects;
        std::size_t _currentTabIndex = 0;
    };
}
