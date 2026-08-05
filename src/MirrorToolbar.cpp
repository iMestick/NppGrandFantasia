#include "MirrorToolbar.h"

#include "npp/Notepad_plus_msgs.h"

#include <algorithm>
#include <array>
#include <commctrl.h>
#include <windowsx.h>
#include <cwchar>
#include <iterator>
#include <utility>
#include <vector>

namespace NppGrandFantasia
{
    namespace
    {
        constexpr wchar_t WindowClassName[] = L"NppGrandFantasia.MirrorToolbar";
        constexpr UINT_PTR ToolbarSubclassId = 0x4E4747U;

        struct ToolbarCandidate
        {
            HWND window = nullptr;
            int width = 0;
            int height = 0;
        };

        BOOL CALLBACK FindToolbarProc(HWND window, LPARAM lParam)
        {
            auto* candidates = reinterpret_cast<std::vector<ToolbarCandidate>*>(lParam);
            if (candidates == nullptr)
            {
                return FALSE;
            }

            wchar_t className[64]{};
            GetClassNameW(window, className, static_cast<int>(std::size(className)));
            if (_wcsicmp(className, TOOLBARCLASSNAMEW) != 0 || IsWindowVisible(window) == FALSE)
            {
                return TRUE;
            }

            RECT client{};
            GetClientRect(window, &client);
            const int width = client.right - client.left;
            const int height = client.bottom - client.top;
            if (width > 0 && height > 0)
            {
                candidates->push_back({window, width, height});
            }
            return TRUE;
        }

        COLORREF Mix(COLORREF first, COLORREF second, int secondWeight)
        {
            secondWeight = std::clamp(secondWeight, 0, 255);
            const int firstWeight = 255 - secondWeight;
            return RGB(
                (GetRValue(first) * firstWeight + GetRValue(second) * secondWeight) / 255,
                (GetGValue(first) * firstWeight + GetGValue(second) * secondWeight) / 255,
                (GetBValue(first) * firstWeight + GetBValue(second) * secondWeight) / 255);
        }
    }

    MirrorToolbar::~MirrorToolbar()
    {
        Destroy();
    }

    bool MirrorToolbar::Create(
        HINSTANCE instance,
        HWND notepadHandle,
        LinkCallback linkCallback,
        SyncCallback syncCallback,
        void* callbackContext)
    {
        if (_window != nullptr)
        {
            return true;
        }

        _instance = instance;
        _notepadHandle = notepadHandle;
        _linkCallback = linkCallback;
        _syncCallback = syncCallback;
        _callbackContext = callbackContext;

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = _instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = WindowClassName;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }

        _toolbar = FindNotepadToolbar();
        if (_toolbar == nullptr)
        {
            return false;
        }

        _window = CreateWindowExW(
            0,
            WindowClassName,
            L"",
            WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP,
            0,
            0,
            Scale(132),
            Scale(22),
            _toolbar,
            nullptr,
            _instance,
            this);
        if (_window == nullptr)
        {
            _toolbar = nullptr;
            return false;
        }

        const HFONT toolbarFont = reinterpret_cast<HFONT>(SendMessageW(_toolbar, WM_GETFONT, 0, 0));
        if (toolbarFont != nullptr)
        {
            SendMessageW(_window, WM_SETFONT, reinterpret_cast<WPARAM>(toolbarFont), FALSE);
        }

        if (SetWindowSubclass(
                _toolbar,
                ToolbarSubclassProc,
                ToolbarSubclassId,
                reinterpret_cast<DWORD_PTR>(this)) == FALSE)
        {
            DestroyWindow(_window);
            _window = nullptr;
            _toolbar = nullptr;
            return false;
        }

        _tooltip = CreateWindowExW(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            _window,
            nullptr,
            _instance,
            nullptr);
        if (_tooltip != nullptr)
        {
            TOOLINFOW tool{};
            tool.cbSize = sizeof(tool);
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tool.hwnd = _toolbar;
            tool.uId = reinterpret_cast<UINT_PTR>(_window);
            tool.lpszText = _tooltipText.data();
            SendMessageW(_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }

        Reposition();
        InvalidateRect(_window, nullptr, TRUE);
        return true;
    }

    void MirrorToolbar::Destroy()
    {
        if (_tooltip != nullptr)
        {
            DestroyWindow(_tooltip);
            _tooltip = nullptr;
        }
        if (_toolbar != nullptr && IsWindow(_toolbar) != FALSE)
        {
            RemoveWindowSubclass(_toolbar, ToolbarSubclassProc, ToolbarSubclassId);
        }
        if (_window != nullptr && IsWindow(_window) != FALSE)
        {
            DestroyWindow(_window);
        }

        _window = nullptr;
        _toolbar = nullptr;
        _notepadHandle = nullptr;
        _linkCallback = nullptr;
        _syncCallback = nullptr;
        _callbackContext = nullptr;
    }

    void MirrorToolbar::SetState(MirrorToolbarState state, std::wstring tooltipText)
    {
        _state = state;
        if (!tooltipText.empty())
        {
            _tooltipText = std::move(tooltipText);
        }
        UpdateTooltip();
        // Alterar o estado visual nao deve recalcular o layout. Durante a
        // digitacao, Syncing/Linked muda com frequencia e apenas o conteudo do
        // bloco precisa ser repintado. A posicao e atualizada somente por resize,
        // DPI/theme ou EnsureLayout explicito.
        if (_window != nullptr)
        {
            InvalidateRect(_window, nullptr, FALSE);
        }
    }

    void MirrorToolbar::ApplyTheme()
    {
        if (_window != nullptr)
        {
            InvalidateRect(_window, nullptr, TRUE);
        }
    }

    void MirrorToolbar::EnsureLayout()
    {
        Reposition();
        if (_window != nullptr)
        {
            InvalidateRect(_window, nullptr, FALSE);
        }
    }

    bool MirrorToolbar::IsCreated() const
    {
        return _window != nullptr;
    }

    int MirrorToolbar::ReservedWidthLogical() const
    {
        return 138;
    }

    LRESULT CALLBACK MirrorToolbar::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        MirrorToolbar* self = reinterpret_cast<MirrorToolbar*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<MirrorToolbar*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->_window = window;
        }
        if (self != nullptr)
        {
            const LRESULT result = self->HandleMessage(message, wParam, lParam);
            if (message == WM_NCDESTROY)
            {
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                self->_window = nullptr;
            }
            return result;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT CALLBACK MirrorToolbar::ToolbarSubclassProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR referenceData)
    {
        auto* self = reinterpret_cast<MirrorToolbar*>(referenceData);
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(window, ToolbarSubclassProc, subclassId);
            if (self != nullptr)
            {
                self->_toolbar = nullptr;
            }
            return DefSubclassProc(window, message, wParam, lParam);
        }
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (self != nullptr &&
            (message == WM_SIZE || message == WM_WINDOWPOSCHANGED || message == TB_AUTOSIZE ||
             message == WM_DPICHANGED || message == WM_THEMECHANGED))
        {
            self->Reposition();
        }
        return result;
    }

    LRESULT MirrorToolbar::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(_window, &paint);
            RECT client{};
            GetClientRect(_window, &client);
            Paint(dc, client);
            EndPaint(_window, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONUP:
        {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (PointInSyncButton(point))
            {
                if (_state == MirrorToolbarState::Linked || _state == MirrorToolbarState::Error)
                {
                    if (_syncCallback != nullptr)
                    {
                        _syncCallback(_callbackContext);
                    }
                }
            }
            else if (_linkCallback != nullptr && _state != MirrorToolbarState::Syncing)
            {
                _linkCallback(_callbackContext);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            if (!_mouseTracking)
            {
                TRACKMOUSEEVENT tracking{};
                tracking.cbSize = sizeof(tracking);
                tracking.dwFlags = TME_LEAVE;
                tracking.hwndTrack = _window;
                TrackMouseEvent(&tracking);
                _mouseTracking = true;
            }
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const bool newHoverSync = PointInSyncButton(point);
            const bool newHoverLink = !newHoverSync;
            if (_hoverSync != newHoverSync || _hoverLink != newHoverLink)
            {
                _hoverSync = newHoverSync;
                _hoverLink = newHoverLink;
                InvalidateRect(_window, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            _mouseTracking = false;
            _hoverLink = false;
            _hoverSync = false;
            InvalidateRect(_window, nullptr, FALSE);
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        case WM_KEYDOWN:
            if (wParam == VK_RETURN || wParam == VK_SPACE)
            {
                if (_linkCallback != nullptr && _state != MirrorToolbarState::Syncing)
                {
                    _linkCallback(_callbackContext);
                }
                return 0;
            }
            break;
        }
        return DefWindowProcW(_window, message, wParam, lParam);
    }

    HWND MirrorToolbar::FindNotepadToolbar() const
    {
        std::vector<ToolbarCandidate> candidates;
        EnumChildWindows(_notepadHandle, FindToolbarProc, reinterpret_cast<LPARAM>(&candidates));
        if (candidates.empty())
        {
            return nullptr;
        }
        const auto best = std::max_element(
            candidates.begin(), candidates.end(),
            [](const ToolbarCandidate& left, const ToolbarCandidate& right)
            {
                return left.width != right.width ? left.width < right.width : left.height < right.height;
            });
        return best->window;
    }

    void MirrorToolbar::Reposition()
    {
        if (_toolbar == nullptr || _window == nullptr ||
            IsWindow(_toolbar) == FALSE || IsWindow(_window) == FALSE)
        {
            return;
        }
        RECT client{};
        GetClientRect(_toolbar, &client);
        const int toolbarWidth = client.right - client.left;
        const int toolbarHeight = client.bottom - client.top;
        const int width = Scale(132);
        const int height = std::min(Scale(22), std::max(1, toolbarHeight - Scale(2)));
        const int left = toolbarWidth - Scale(4) - width;
        if (toolbarWidth <= 0 || toolbarHeight <= 0 || left < Scale(8))
        {
            // Durante autosize/save o toolbar pode reportar uma geometria
            // transitoria. Mantemos o ultimo layout em vez de ocultar o bloco.
            return;
        }
        const int top = std::max(0, (toolbarHeight - height) / 2);
        SetWindowPos(_window, HWND_TOP, left, top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    void MirrorToolbar::Paint(HDC dc, const RECT& clientRect)
    {
        const bool darkMode = _notepadHandle != nullptr &&
            SendMessageW(_notepadHandle, NPPM_ISDARKMODEENABLED, 0, 0) != FALSE;
        COLORREF background = GetSysColor(COLOR_BTNFACE);
        COLORREF text = GetSysColor(COLOR_BTNTEXT);
        COLORREF muted = GetSysColor(COLOR_GRAYTEXT);
        COLORREF edge = GetSysColor(COLOR_3DSHADOW);
        COLORREF hot = GetSysColor(COLOR_HIGHLIGHT);
        if (darkMode)
        {
            NppDarkMode::Colors colors{};
            if (SendMessageW(_notepadHandle, NPPM_GETDARKMODECOLORS, sizeof(colors), reinterpret_cast<LPARAM>(&colors)) != FALSE)
            {
                background = colors.softerBackground;
                text = colors.text;
                muted = colors.darkerText;
                edge = colors.edge;
                hot = colors.hotBackground;
            }
            else
            {
                background = RGB(45, 45, 48);
                text = RGB(230, 230, 230);
                muted = RGB(155, 155, 155);
                edge = RGB(80, 80, 80);
                hot = RGB(62, 62, 66);
            }
        }

        HBRUSH bg = CreateSolidBrush(background);
        FillRect(dc, &clientRect, bg);
        DeleteObject(bg);
        HBRUSH edgeBrush = CreateSolidBrush(edge);
        RECT frame = clientRect;
        FrameRect(dc, &frame, edgeBrush);
        DeleteObject(edgeBrush);

        _syncButton = clientRect;
        _syncButton.left = clientRect.right - Scale(42);
        RECT divider = clientRect;
        divider.left = _syncButton.left;
        divider.right = divider.left + 1;
        FillRect(dc, &divider, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

        if (_hoverLink && _state != MirrorToolbarState::Syncing)
        {
            RECT hover = clientRect;
            hover.right = _syncButton.left;
            InflateRect(&hover, -1, -1);
            HBRUSH hotBrush = CreateSolidBrush(hot);
            FillRect(dc, &hover, hotBrush);
            DeleteObject(hotBrush);
        }
        if (_hoverSync && (_state == MirrorToolbarState::Linked || _state == MirrorToolbarState::Error))
        {
            RECT hover = _syncButton;
            InflateRect(&hover, -1, -1);
            HBRUSH hotBrush = CreateSolidBrush(hot);
            FillRect(dc, &hover, hotBrush);
            DeleteObject(hotBrush);
        }

        COLORREF dotColor = muted;
        const wchar_t* linkText = L"Vincular";
        switch (_state)
        {
        case MirrorToolbarState::Linked:
            dotColor = RGB(70, 190, 95);
            linkText = L"S→C";
            break;
        case MirrorToolbarState::Syncing:
            dotColor = RGB(70, 150, 235);
            linkText = L"Sync...";
            break;
        case MirrorToolbarState::Error:
            dotColor = RGB(235, 75, 75);
            linkText = L"Erro";
            break;
        case MirrorToolbarState::Unlinked:
        default:
            break;
        }

        const int dotSize = Scale(7);
        const int dotTop = (clientRect.bottom - dotSize) / 2;
        HBRUSH dotBrush = CreateSolidBrush(dotColor);
        HPEN dotPen = CreatePen(PS_SOLID, 1, Mix(dotColor, RGB(0, 0, 0), 65));
        HGDIOBJ oldBrush = SelectObject(dc, dotBrush);
        HGDIOBJ oldPen = SelectObject(dc, dotPen);
        Ellipse(dc, Scale(7), dotTop, Scale(7) + dotSize, dotTop + dotSize);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(dotPen);
        DeleteObject(dotBrush);

        SetBkMode(dc, TRANSPARENT);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(_window, WM_GETFONT, 0, 0));
        if (font == nullptr)
        {
            font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        HGDIOBJ oldFont = SelectObject(dc, font);
        RECT linkRect = clientRect;
        linkRect.left = Scale(19);
        linkRect.right = _syncButton.left - Scale(3);
        SetTextColor(dc, _state == MirrorToolbarState::Syncing ? muted : text);
        DrawTextW(dc, linkText, -1, &linkRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        SetTextColor(dc,
            (_state == MirrorToolbarState::Linked || _state == MirrorToolbarState::Error) ? text : muted);
        DrawTextW(dc, L"Sync", -1, &_syncButton, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    }

    void MirrorToolbar::UpdateTooltip()
    {
        if (_tooltip == nullptr || _toolbar == nullptr || _window == nullptr)
        {
            return;
        }
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_IDISHWND;
        tool.hwnd = _toolbar;
        tool.uId = reinterpret_cast<UINT_PTR>(_window);
        tool.lpszText = _tooltipText.data();
        SendMessageW(_tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }

    int MirrorToolbar::Scale(int value) const
    {
        UINT dpi = 96;
        using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFunction>(
            user32 != nullptr ? GetProcAddress(user32, "GetDpiForWindow") : nullptr);
        if (getDpiForWindow != nullptr && _notepadHandle != nullptr)
        {
            dpi = getDpiForWindow(_notepadHandle);
        }
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    bool MirrorToolbar::PointInSyncButton(POINT point) const
    {
        return PtInRect(&_syncButton, point) != FALSE;
    }
}
