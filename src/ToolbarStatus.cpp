#include "ToolbarStatus.h"

#include "npp/Notepad_plus_msgs.h"

#include <algorithm>
#include <commctrl.h>
#include <windowsx.h>
#include <cwchar>
#include <iterator>
#include <sstream>
#include <utility>
#include <vector>

namespace NppGrandFantasia
{
    namespace
    {
        constexpr wchar_t WindowClassName[] = L"NppGrandFantasia.ToolbarStatus";
        constexpr UINT_PTR ToolbarSubclassId = 0x4E4746U;
        constexpr wchar_t DefaultTooltipText[] =
            L"Clique em uma linha para ir ate o erro. Botao direito abre ou oculta o painel completo.";

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

        int TextWidth(HDC deviceContext, const std::wstring& text)
        {
            if (text.empty())
            {
                return 0;
            }

            SIZE size{};
            if (GetTextExtentPoint32W(
                    deviceContext,
                    text.c_str(),
                    static_cast<int>(text.size()),
                    &size) == FALSE)
            {
                return 0;
            }
            return size.cx;
        }
    }

    ToolbarStatus::~ToolbarStatus()
    {
        Destroy();
    }

    bool ToolbarStatus::Create(
        HINSTANCE instance,
        HWND notepadHandle,
        NavigateCallback navigateCallback,
        TogglePanelCallback togglePanelCallback,
        void* callbackContext)
    {
        if (_window != nullptr)
        {
            return true;
        }

        _instance = instance;
        _notepadHandle = notepadHandle;
        _navigateCallback = navigateCallback;
        _togglePanelCallback = togglePanelCallback;
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
            Scale(320),
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
            tool.lpszText = const_cast<wchar_t*>(DefaultTooltipText);
            SendMessageW(_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }

        ApplyTheme();
        UpdateTooltipText();
        Reposition();
        return true;
    }

    void ToolbarStatus::Destroy()
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
        _navigateCallback = nullptr;
        _togglePanelCallback = nullptr;
        _callbackContext = nullptr;
        _errorLines.clear();
        _clickRegions.clear();
        _hoveredError = -1;
    }

    void ToolbarStatus::SetStatus(std::wstring text, bool validationActive, bool hasError)
    {
        _text = std::move(text);
        _validationActive = validationActive;
        _hasError = hasError;
        _errorLines.clear();
        _clickRegions.clear();
        _hoveredError = -1;

        if (_window != nullptr)
        {
            SetWindowTextW(_window, _text.c_str());
            // O conteudo muda em tempo real; o retangulo do controle nao.
            // Evita SetWindowPos a cada validacao/digitacao.
            InvalidateRect(_window, nullptr, TRUE);
            UpdateTooltipText();
        }
    }

    void ToolbarStatus::SetErrorLines(
        std::vector<ToolbarErrorLink> errorLines,
        bool validationActive)
    {
        std::sort(
            errorLines.begin(),
            errorLines.end(),
            [](const ToolbarErrorLink& left, const ToolbarErrorLink& right)
            {
                if (left.startLine != right.startLine)
                {
                    return left.startLine < right.startLine;
                }
                return left.endLine < right.endLine;
            });

        _errorLines = std::move(errorLines);
        _validationActive = validationActive;
        _hasError = !_errorLines.empty();
        _text = _hasError ? L"Linhas quebradas" : L"Nenhuma linha quebrada";
        _clickRegions.clear();
        _hoveredError = -1;

        if (_window != nullptr)
        {
            SetWindowTextW(_window, _text.c_str());
            // A lista muda em tempo real, mas o layout permanece estavel.
            InvalidateRect(_window, nullptr, TRUE);
            UpdateTooltipText();
        }
    }

    void ToolbarStatus::SetErrorColor(COLORREF color)
    {
        _errorColor = color;
        if (_window != nullptr)
        {
            InvalidateRect(_window, nullptr, TRUE);
        }
    }

    void ToolbarStatus::SetRightReservedWidthLogical(int logicalPixels)
    {
        _rightReservedWidthLogical = std::max(0, logicalPixels);
        Reposition();
    }

    void ToolbarStatus::ApplyTheme()
    {
        if (_window != nullptr)
        {
            InvalidateRect(_window, nullptr, TRUE);
        }
    }

    void ToolbarStatus::EnsureLayout()
    {
        if (_window != nullptr && IsWindow(_window) != FALSE)
        {
            // Mantem a ultima geometria visivel mesmo se a toolbar nativa
            // estiver em um autosize transitorio durante a sincronizacao.
            ShowWindow(_window, SW_SHOWNOACTIVATE);
        }

        Reposition();

        if (_window != nullptr && IsWindow(_window) != FALSE)
        {
            // Repinta somente o bloco personalizado e conclui o desenho antes
            // de devolver o controle ao editor. RDW_NOERASE evita flicker.
            RedrawWindow(
                _window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        }
    }

    bool ToolbarStatus::IsCreated() const
    {
        return _window != nullptr;
    }

    LRESULT CALLBACK ToolbarStatus::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        ToolbarStatus* self = reinterpret_cast<ToolbarStatus*>(GetWindowLongPtrW(window, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<ToolbarStatus*>(create->lpCreateParams);
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

    LRESULT CALLBACK ToolbarStatus::ToolbarSubclassProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR referenceData)
    {
        auto* self = reinterpret_cast<ToolbarStatus*>(referenceData);

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
        if (self == nullptr)
        {
            return result;
        }

        switch (message)
        {
        case WM_SIZE:
        case WM_WINDOWPOSCHANGED:
        case WM_STYLECHANGED:
            self->Reposition();
            break;
        }

        return result;
    }

    LRESULT ToolbarStatus::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC deviceContext = BeginPaint(_window, &paint);
            RECT client{};
            GetClientRect(_window, &client);
            Paint(deviceContext, client);
            EndPaint(_window, &paint);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_LBUTTONUP:
        {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            NavigateFromPoint(point);
            return 0;
        }

        case WM_RBUTTONUP:
            if (_togglePanelCallback != nullptr)
            {
                _togglePanelCallback(_callbackContext);
            }
            return 0;

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
            UpdateHover(point);
            return 0;
        }

        case WM_MOUSELEAVE:
            _mouseTracking = false;
            if (_hoveredError != -1)
            {
                _hoveredError = -1;
                InvalidateRect(_window, nullptr, FALSE);
            }
            return 0;

        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(_window, nullptr, FALSE);
            return 0;

        case WM_SETCURSOR:
        {
            const bool canNavigate = !_errorLines.empty();
            SetCursor(LoadCursorW(nullptr, canNavigate ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }

        case WM_GETDLGCODE:
            return DLGC_WANTCHARS;

        case WM_KEYDOWN:
            if ((wParam == VK_RETURN || wParam == VK_SPACE) &&
                !_errorLines.empty() &&
                _navigateCallback != nullptr)
            {
                _navigateCallback(_callbackContext, _errorLines.front().startLine);
                return 0;
            }
            break;

        case WM_DESTROY:
            return 0;
        }

        return DefWindowProcW(_window, message, wParam, lParam);
    }

    HWND ToolbarStatus::FindNotepadToolbar() const
    {
        if (_notepadHandle == nullptr)
        {
            return nullptr;
        }

        std::vector<ToolbarCandidate> candidates;
        EnumChildWindows(_notepadHandle, FindToolbarProc, reinterpret_cast<LPARAM>(&candidates));
        if (candidates.empty())
        {
            return nullptr;
        }

        const auto best = std::max_element(
            candidates.begin(),
            candidates.end(),
            [](const ToolbarCandidate& left, const ToolbarCandidate& right)
            {
                if (left.width != right.width)
                {
                    return left.width < right.width;
                }
                return left.height < right.height;
            });
        return best->window;
    }

    void ToolbarStatus::Reposition()
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
        if (toolbarWidth <= 0 || toolbarHeight <= 0)
        {
            // O toolbar nativo pode passar brevemente por tamanho zero durante
            // save/theme/autosize. Nao escondemos o controle nesse estado
            // transitorio, pois ele poderia permanecer oculto sem novo WM_SIZE.
            return;
        }

        int lastButtonRight = 0;
        const LRESULT buttonCount = SendMessageW(_toolbar, TB_BUTTONCOUNT, 0, 0);
        for (LRESULT index = 0; index < buttonCount; ++index)
        {
            RECT button{};
            if (SendMessageW(
                    _toolbar,
                    TB_GETITEMRECT,
                    static_cast<WPARAM>(index),
                    reinterpret_cast<LPARAM>(&button)) != FALSE)
            {
                lastButtonRight = std::max(lastButtonRight, static_cast<int>(button.right));
            }
        }

        const int sidePadding = Scale(4);
        const int buttonGap = Scale(8);
        const int minimumWidth = Scale(180);
        const int desiredWidth = Scale(320);
        const int desiredHeight = std::min(Scale(22), std::max(1, toolbarHeight - Scale(2)));
        const int right = toolbarWidth - sidePadding - Scale(_rightReservedWidthLogical);
        const int minimumLeft = lastButtonRight + buttonGap;
        const int availableWidth = right - minimumLeft;

        if (availableWidth < minimumWidth)
        {
            // Preserve o ultimo layout em geometrias transitorias. Quando houver
            // algum espaco real, reduzimos de forma controlada sem desaparecer.
            if (availableWidth < Scale(96))
            {
                return;
            }
        }

        const int width = std::min(desiredWidth, availableWidth);
        const int left = right - width;
        const int top = std::max(0, (toolbarHeight - desiredHeight) / 2);

        SetWindowPos(
            _window,
            HWND_TOP,
            left,
            top,
            width,
            desiredHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    void ToolbarStatus::Paint(HDC deviceContext, const RECT& clientRect)
    {
        const bool darkMode = _notepadHandle != nullptr &&
            SendMessageW(_notepadHandle, NPPM_ISDARKMODEENABLED, 0, 0) != FALSE;

        COLORREF background = GetSysColor(COLOR_BTNFACE);
        COLORREF text = GetSysColor(COLOR_BTNTEXT);
        COLORREF mutedText = GetSysColor(COLOR_GRAYTEXT);
        COLORREF edge = GetSysColor(COLOR_3DSHADOW);

        if (darkMode)
        {
            NppDarkMode::Colors colors{};
            if (SendMessageW(
                    _notepadHandle,
                    NPPM_GETDARKMODECOLORS,
                    sizeof(colors),
                    reinterpret_cast<LPARAM>(&colors)) != FALSE)
            {
                background = colors.softerBackground;
                text = colors.text;
                mutedText = colors.darkerText;
                edge = colors.edge;
            }
            else
            {
                background = RGB(45, 45, 48);
                text = RGB(230, 230, 230);
                mutedText = RGB(160, 160, 160);
                edge = RGB(80, 80, 80);
            }
        }

        HBRUSH backgroundBrush = CreateSolidBrush(background);
        FillRect(deviceContext, &clientRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        HBRUSH edgeBrush = CreateSolidBrush(edge);
        RECT edgeRect = clientRect;
        FrameRect(deviceContext, &edgeRect, edgeBrush);
        DeleteObject(edgeBrush);

        const int indicatorSize = Scale(7);
        const int leftPadding = Scale(7);
        const int indicatorTop = (clientRect.bottom - clientRect.top - indicatorSize) / 2;
        RECT indicator{
            leftPadding,
            indicatorTop,
            leftPadding + indicatorSize,
            indicatorTop + indicatorSize,
        };

        COLORREF indicatorColor = mutedText;
        if (_validationActive)
        {
            indicatorColor = _hasError ? _errorColor : RGB(70, 190, 95);
        }

        HBRUSH indicatorBrush = CreateSolidBrush(indicatorColor);
        HPEN indicatorPen = CreatePen(PS_SOLID, 1, Mix(indicatorColor, RGB(0, 0, 0), 60));
        const HGDIOBJ oldBrush = SelectObject(deviceContext, indicatorBrush);
        const HGDIOBJ oldPen = SelectObject(deviceContext, indicatorPen);
        Ellipse(deviceContext, indicator.left, indicator.top, indicator.right, indicator.bottom);
        SelectObject(deviceContext, oldPen);
        SelectObject(deviceContext, oldBrush);
        DeleteObject(indicatorPen);
        DeleteObject(indicatorBrush);

        RECT textBounds = clientRect;
        textBounds.left = indicator.right + Scale(6);
        textBounds.right -= Scale(6);

        SetBkMode(deviceContext, TRANSPARENT);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(_window, WM_GETFONT, 0, 0));
        if (font == nullptr)
        {
            font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        const HGDIOBJ oldFont = SelectObject(deviceContext, font);

        if (_validationActive && !_errorLines.empty())
        {
            PaintErrorLines(deviceContext, textBounds, text, mutedText);
        }
        else
        {
            _clickRegions.clear();
            SetTextColor(deviceContext, _validationActive ? text : mutedText);
            DrawTextW(
                deviceContext,
                _text.c_str(),
                static_cast<int>(_text.size()),
                &textBounds,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        SelectObject(deviceContext, oldFont);

        if (GetFocus() == _window)
        {
            RECT focus = clientRect;
            InflateRect(&focus, -2, -2);
            DrawFocusRect(deviceContext, &focus);
        }
    }

    void ToolbarStatus::PaintErrorLines(
        HDC deviceContext,
        const RECT& textBounds,
        COLORREF textColor,
        COLORREF mutedTextColor)
    {
        _clickRegions.clear();

        const std::wstring prefix = L"Linhas: ";
        const int prefixWidth = TextWidth(deviceContext, prefix);
        const int height = textBounds.bottom - textBounds.top;
        const int textY = textBounds.top;
        int x = textBounds.left;

        RECT prefixRect{x, textY, textBounds.right, textY + height};
        SetTextColor(deviceContext, textColor);
        DrawTextW(
            deviceContext,
            prefix.c_str(),
            static_cast<int>(prefix.size()),
            &prefixRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        x += prefixWidth;

        const std::wstring separator = L", ";
        const int separatorWidth = TextWidth(deviceContext, separator);
        const int ellipsisWidth = TextWidth(deviceContext, L"...");

        for (std::size_t index = 0; index < _errorLines.size(); ++index)
        {
            const std::wstring token = FormatLineRange(_errorLines[index]);
            const int tokenWidth = TextWidth(deviceContext, token);
            const int separatorBefore = index == 0 ? 0 : separatorWidth;
            const bool hasMore = index + 1U < _errorLines.size();
            const int reserve = hasMore ? ellipsisWidth : 0;

            if (x + separatorBefore + tokenWidth + reserve > textBounds.right)
            {
                RECT ellipsisRect{x, textY, textBounds.right, textY + height};
                SetTextColor(deviceContext, mutedTextColor);
                DrawTextW(
                    deviceContext,
                    L"...",
                    3,
                    &ellipsisRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                break;
            }

            if (index != 0)
            {
                RECT separatorRect{x, textY, x + separatorWidth, textY + height};
                SetTextColor(deviceContext, mutedTextColor);
                DrawTextW(
                    deviceContext,
                    separator.c_str(),
                    static_cast<int>(separator.size()),
                    &separatorRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                x += separatorWidth;
            }

            RECT tokenRect{x, textY, x + tokenWidth, textY + height};
            SetTextColor(deviceContext, textColor);
            DrawTextW(
                deviceContext,
                token.c_str(),
                static_cast<int>(token.size()),
                &tokenRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            ClickRegion region{};
            region.bounds = tokenRect;
            InflateRect(&region.bounds, Scale(2), 0);
            region.oneBasedLine = _errorLines[index].startLine;
            region.errorIndex = index;
            _clickRegions.push_back(region);

            if (_hoveredError == static_cast<int>(index))
            {
                const int underlineY = textBounds.top + height - Scale(4);
                HPEN underlinePen = CreatePen(PS_SOLID, 1, _errorColor);
                const HGDIOBJ oldPen = SelectObject(deviceContext, underlinePen);
                MoveToEx(deviceContext, tokenRect.left, underlineY, nullptr);
                LineTo(deviceContext, tokenRect.right, underlineY);
                SelectObject(deviceContext, oldPen);
                DeleteObject(underlinePen);
            }

            x += tokenWidth;
        }
    }

    void ToolbarStatus::UpdateTooltipText()
    {
        if (_tooltip == nullptr || _window == nullptr || _toolbar == nullptr)
        {
            return;
        }

        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_IDISHWND;
        tool.hwnd = _toolbar;
        tool.uId = reinterpret_cast<UINT_PTR>(_window);

        std::wostringstream tooltip;
        if (_validationActive)
        {
            if (_errorLines.empty())
            {
                tooltip << L"Nenhuma linha quebrada.";
            }
            else
            {
                tooltip << L"Linhas quebradas: ";
                for (std::size_t index = 0; index < _errorLines.size(); ++index)
                {
                    if (index != 0)
                    {
                        tooltip << L", ";
                    }
                    tooltip << FormatLineRange(_errorLines[index]);
                }
            }
        }
        else
        {
            tooltip << _text;
        }

        tooltip << L"\r\n" << DefaultTooltipText;
        _tooltipText = tooltip.str();
        tool.lpszText = _tooltipText.data();
        SendMessageW(_tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }

    void ToolbarStatus::NavigateFromPoint(POINT point)
    {
        if (_errorLines.empty() || _navigateCallback == nullptr)
        {
            return;
        }

        const int hit = HitTestError(point);
        const std::size_t line = hit >= 0
            ? _clickRegions[static_cast<std::size_t>(hit)].oneBasedLine
            : _errorLines.front().startLine;
        _navigateCallback(_callbackContext, line);
    }

    int ToolbarStatus::HitTestError(POINT point) const
    {
        for (std::size_t index = 0; index < _clickRegions.size(); ++index)
        {
            if (PtInRect(&_clickRegions[index].bounds, point) != FALSE)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    void ToolbarStatus::UpdateHover(POINT point)
    {
        const int hitRegion = HitTestError(point);
        int newHoveredError = -1;
        if (hitRegion >= 0)
        {
            newHoveredError = static_cast<int>(
                _clickRegions[static_cast<std::size_t>(hitRegion)].errorIndex);
        }

        if (_hoveredError != newHoveredError)
        {
            _hoveredError = newHoveredError;
            InvalidateRect(_window, nullptr, FALSE);
        }
    }

    std::wstring ToolbarStatus::FormatLineRange(const ToolbarErrorLink& error)
    {
        if (error.endLine > error.startLine)
        {
            return std::to_wstring(error.startLine) + L"-" + std::to_wstring(error.endLine);
        }
        return std::to_wstring(error.startLine);
    }

    int ToolbarStatus::Scale(int value) const
    {
        UINT dpi = 96;
        if (_notepadHandle != nullptr)
        {
            using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
            const HMODULE user32 = GetModuleHandleW(L"user32.dll");
            const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFunction>(
                user32 != nullptr ? GetProcAddress(user32, "GetDpiForWindow") : nullptr);
            if (getDpiForWindow != nullptr)
            {
                dpi = getDpiForWindow(_notepadHandle);
            }
        }

        return MulDiv(value, static_cast<int>(dpi), 96);
    }
}
