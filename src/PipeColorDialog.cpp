#include "PipeColorDialog.h"

#include "resource.h"
#include "npp/Notepad_plus_msgs.h"

#include <algorithm>
#include <array>
#include <commdlg.h>
#include <string>

namespace NppGrandFantasia
{
    namespace
    {
        constexpr std::array<int, 4> ColorButtonIds{
            IDC_PIPE_COLOR_1,
            IDC_PIPE_COLOR_2,
            IDC_PIPE_COLOR_3,
            IDC_PIPE_COLOR_4,
        };

        COLORREF ContrastingTextColor(COLORREF color)
        {
            const int luminance =
                GetRValue(color) * 299 +
                GetGValue(color) * 587 +
                GetBValue(color) * 114;
            return luminance >= 145000 ? RGB(0, 0, 0) : RGB(255, 255, 255);
        }
    }

    bool PipeColorDialog::Show(
        HINSTANCE instance,
        HWND owner,
        HWND notepadHandle,
        PipeColorSettings& settings)
    {
        Context context{settings, settings, notepadHandle};
        const INT_PTR result = DialogBoxParamW(
            instance,
            MAKEINTRESOURCEW(IDD_PIPE_COLORS),
            owner,
            DialogProc,
            reinterpret_cast<LPARAM>(&context));

        if (result == IDOK)
        {
            settings = context.working;
            return true;
        }

        return false;
    }

    INT_PTR CALLBACK PipeColorDialog::DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
    {
        Context* context = reinterpret_cast<Context*>(GetWindowLongPtrW(dialog, DWLP_USER));

        if (message == WM_INITDIALOG)
        {
            context = reinterpret_cast<Context*>(lParam);
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
            Initialize(dialog, *context);
            return TRUE;
        }

        if (context == nullptr)
        {
            return FALSE;
        }

        switch (message)
        {
        case WM_COMMAND:
        {
            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);

            if (id == IDC_PIPE_COLOR_COUNT && notification == CBN_SELCHANGE)
            {
                const LRESULT selection = SendDlgItemMessageW(dialog, IDC_PIPE_COLOR_COUNT, CB_GETCURSEL, 0, 0);
                if (selection >= 0)
                {
                    context->working.colorCount = static_cast<int>(selection) + 1;
                    UpdateEnabledColors(dialog, *context);
                }
                return TRUE;
            }

            for (std::size_t index = 0; index < ColorButtonIds.size(); ++index)
            {
                if (id == ColorButtonIds[index] && notification == BN_CLICKED)
                {
                    ChoosePipeColor(dialog, *context, static_cast<int>(index));
                    return TRUE;
                }
            }

            if (id == IDC_VALID_ID_COLOR && notification == BN_CLICKED)
            {
                ChooseValidIdColor(dialog, *context);
                return TRUE;
            }

            if (id == IDC_BROKEN_TEXT_COLOR && notification == BN_CLICKED)
            {
                ChooseBrokenTextColor(dialog, *context);
                return TRUE;
            }

            switch (id)
            {
            case IDC_PIPE_COLORS_DEFAULT:
                context->working = DefaultPipeColorSettings();
                SendDlgItemMessageW(
                    dialog,
                    IDC_PIPE_COLOR_COUNT,
                    CB_SETCURSEL,
                    static_cast<WPARAM>(context->working.colorCount - 1),
                    0);
                UpdateEnabledColors(dialog, *context);
                InvalidateRect(GetDlgItem(dialog, IDC_VALID_ID_COLOR), nullptr, TRUE);
                InvalidateRect(GetDlgItem(dialog, IDC_BROKEN_TEXT_COLOR), nullptr, TRUE);
                return TRUE;

            case IDOK:
                EndDialog(dialog, IDOK);
                return TRUE;

            case IDCANCEL:
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;
        }

        case WM_DRAWITEM:
        {
            const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (draw == nullptr)
            {
                return FALSE;
            }

            for (std::size_t index = 0; index < ColorButtonIds.size(); ++index)
            {
                if (static_cast<int>(draw->CtlID) == ColorButtonIds[index])
                {
                    const std::wstring label = L"Cor " + std::to_wstring(index + 1U);
                    DrawColorButton(
                        *draw,
                        context->working.colors[index],
                        label.c_str(),
                        static_cast<int>(index) < context->working.colorCount);
                    return TRUE;
                }
            }

            if (draw->CtlID == IDC_VALID_ID_COLOR)
            {
                DrawColorButton(
                    *draw,
                    context->working.validIdColor,
                    L"IDs validos",
                    true);
                return TRUE;
            }

            if (draw->CtlID == IDC_BROKEN_TEXT_COLOR)
            {
                DrawColorButton(
                    *draw,
                    context->working.brokenTextColor,
                    L"Caracteres das linhas quebradas",
                    true);
                return TRUE;
            }
            break;
        }

        case WM_CLOSE:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }

        return FALSE;
    }

    void PipeColorDialog::Initialize(HWND dialog, Context& context)
    {
        for (int count = 1; count <= 4; ++count)
        {
            const std::wstring text = std::to_wstring(count) + (count == 1 ? L" cor" : L" cores");
            SendDlgItemMessageW(
                dialog,
                IDC_PIPE_COLOR_COUNT,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(text.c_str()));
        }

        context.working.colorCount = std::clamp(context.working.colorCount, 1, 4);
        SendDlgItemMessageW(
            dialog,
            IDC_PIPE_COLOR_COUNT,
            CB_SETCURSEL,
            static_cast<WPARAM>(context.working.colorCount - 1),
            0);

        SendMessageW(
            context.notepadHandle,
            NPPM_DARKMODESUBCLASSANDTHEME,
            static_cast<WPARAM>(NppDarkMode::dmfInit),
            reinterpret_cast<LPARAM>(dialog));

        UpdateEnabledColors(dialog, context);
    }

    void PipeColorDialog::UpdateEnabledColors(HWND dialog, const Context& context)
    {
        for (std::size_t index = 0; index < ColorButtonIds.size(); ++index)
        {
            const BOOL enabled = static_cast<int>(index) < context.working.colorCount ? TRUE : FALSE;
            EnableWindow(GetDlgItem(dialog, ColorButtonIds[index]), enabled);
            InvalidateRect(GetDlgItem(dialog, ColorButtonIds[index]), nullptr, TRUE);
        }
    }

    void PipeColorDialog::ChoosePipeColor(HWND dialog, Context& context, int colorIndex)
    {
        if (colorIndex < 0 || colorIndex >= context.working.colorCount)
        {
            return;
        }

        COLORREF& color = context.working.colors[static_cast<std::size_t>(colorIndex)];
        if (ShowColorPicker(dialog, color))
        {
            InvalidateRect(
                GetDlgItem(dialog, ColorButtonIds[static_cast<std::size_t>(colorIndex)]),
                nullptr,
                TRUE);
        }
    }

    void PipeColorDialog::ChooseValidIdColor(HWND dialog, Context& context)
    {
        if (ShowColorPicker(dialog, context.working.validIdColor))
        {
            InvalidateRect(GetDlgItem(dialog, IDC_VALID_ID_COLOR), nullptr, TRUE);
        }
    }

    void PipeColorDialog::ChooseBrokenTextColor(HWND dialog, Context& context)
    {
        if (ShowColorPicker(dialog, context.working.brokenTextColor))
        {
            InvalidateRect(GetDlgItem(dialog, IDC_BROKEN_TEXT_COLOR), nullptr, TRUE);
        }
    }

    bool PipeColorDialog::ShowColorPicker(HWND dialog, COLORREF& color)
    {
        static COLORREF customColors[16]{};
        CHOOSECOLORW chooser{};
        chooser.lStructSize = sizeof(chooser);
        chooser.hwndOwner = dialog;
        chooser.rgbResult = color;
        chooser.lpCustColors = customColors;
        chooser.Flags = CC_FULLOPEN | CC_RGBINIT;

        if (!ChooseColorW(&chooser))
        {
            return false;
        }

        color = chooser.rgbResult;
        return true;
    }

    void PipeColorDialog::DrawColorButton(
        const DRAWITEMSTRUCT& draw,
        COLORREF color,
        const wchar_t* label,
        bool enabled)
    {
        RECT bounds = draw.rcItem;
        const COLORREF fill = enabled ? color : GetSysColor(COLOR_BTNFACE);
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(draw.hDC, &bounds, brush);
        DeleteObject(brush);

        FrameRect(draw.hDC, &bounds, GetSysColorBrush(COLOR_WINDOWFRAME));

        SetBkMode(draw.hDC, TRANSPARENT);
        SetTextColor(draw.hDC, enabled ? ContrastingTextColor(color) : GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(
            draw.hDC,
            label,
            -1,
            &bounds,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        if ((draw.itemState & ODS_FOCUS) != 0)
        {
            RECT focus = bounds;
            InflateRect(&focus, -3, -3);
            DrawFocusRect(draw.hDC, &focus);
        }
    }
}
