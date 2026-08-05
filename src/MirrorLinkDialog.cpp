#include "MirrorLinkDialog.h"

#include "npp/Notepad_plus_msgs.h"
#include "resource.h"

#include <algorithm>
#include <commctrl.h>
#include <string>

namespace NppGrandFantasia
{
    MirrorLinkDialog::MirrorLinkDialog(
        HINSTANCE instance,
        NppData nppData,
        MirrorLinkManager& manager)
        : _instance(instance),
          _nppData(nppData),
          _manager(manager)
    {
    }

    bool MirrorLinkDialog::Show(HWND owner)
    {
        return DialogBoxParamW(
            _instance,
            MAKEINTRESOURCEW(IDD_MIRROR_LINKS),
            owner,
            DialogProc,
            reinterpret_cast<LPARAM>(this)) != -1;
    }

    INT_PTR CALLBACK MirrorLinkDialog::DialogProc(
        HWND dialog,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        auto* self = reinterpret_cast<MirrorLinkDialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (message == WM_INITDIALOG)
        {
            self = reinterpret_cast<MirrorLinkDialog*>(lParam);
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
            if (self != nullptr)
            {
                self->_dialog = dialog;
            }
        }
        return self != nullptr ? self->HandleMessage(message, wParam, lParam) : FALSE;
    }

    INT_PTR MirrorLinkDialog::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_INITDIALOG:
            Initialize();
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(_dialog, IDCANCEL);
                return TRUE;
            }
            break;

        case WM_NOTIFY:
        {
            const auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header == nullptr || header->hwndFrom != _list)
            {
                break;
            }
            if (header->code == LVN_ITEMCHANGED)
            {
                const auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
                if (!_updatingChecks && changed != nullptr && changed->iItem >= 0 &&
                    (changed->uChanged & LVIF_STATE) != 0 &&
                    ((changed->uOldState ^ changed->uNewState) & LVIS_STATEIMAGEMASK) != 0)
                {
                    HandleCheckChanged(
                        changed->iItem,
                        ListView_GetCheckState(_list, changed->iItem) != FALSE);
                }
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lParam) == _status && _statusIsError)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, RGB(232, 96, 96));
                SetBkMode(dc, TRANSPARENT);
                return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
            }
            break;

        case WM_CLOSE:
            EndDialog(_dialog, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }

    void MirrorLinkDialog::Initialize()
    {
        _list = GetDlgItem(_dialog, IDC_MIRROR_PAIR_LIST);
        _status = GetDlgItem(_dialog, IDC_MIRROR_STATUS);

        if (_list != nullptr)
        {
            ListView_SetExtendedListViewStyle(
                _list,
                LVS_EX_CHECKBOXES |
                LVS_EX_FULLROWSELECT |
                LVS_EX_DOUBLEBUFFER |
                LVS_EX_LABELTIP);

            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_FMT | LVCF_WIDTH;
            column.fmt = LVCFMT_LEFT;

            wchar_t serverTitle[] = L"Arquivo do servidor";
            column.pszText = serverTitle;
            column.cx = 142;
            ListView_InsertColumn(_list, 0, &column);

            wchar_t clientTitle[] = L"Arquivo do cliente";
            column.pszText = clientTitle;
            column.cx = 142;
            ListView_InsertColumn(_list, 1, &column);

            wchar_t stateTitle[] = L"Estado";
            column.pszText = stateTitle;
            column.cx = 76;
            ListView_InsertColumn(_list, 2, &column);
        }

        ApplyTheme();
        ReloadPairs();
        ResizeColumns();
        FitWindowToRows();
    }

    void MirrorLinkDialog::ReloadPairs()
    {
        if (_list == nullptr)
        {
            return;
        }

        _updatingChecks = true;
        ListView_DeleteAllItems(_list);
        _pairs = _manager.GetAvailablePairs();

        for (std::size_t index = 0; index < _pairs.size(); ++index)
        {
            const MirrorPairCandidate& pair = _pairs[index];
            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = static_cast<int>(index);
            item.pszText = const_cast<wchar_t*>(pair.sourceName.c_str());
            item.lParam = static_cast<LPARAM>(index);
            const int inserted = ListView_InsertItem(_list, &item);
            if (inserted < 0)
            {
                continue;
            }

            ListView_SetItemText(
                _list,
                inserted,
                1,
                const_cast<wchar_t*>(pair.mirrorName.c_str()));

            wchar_t linkedText[] = L"Ativo";
            wchar_t conflictText[] = L"Em uso";
            wchar_t availableText[] = L"Disponivel";
            ListView_SetItemText(
                _list,
                inserted,
                2,
                pair.linked
                    ? linkedText
                    : (pair.conflict ? conflictText : availableText));
            ListView_SetCheckState(_list, inserted, pair.linked ? TRUE : FALSE);
        }
        _updatingChecks = false;

        if (_pairs.empty())
        {
            SetStatus(L"Abra os arquivos S_ e C_ correspondentes para criar um vinculo.");
        }
        else if (_manager.LinkCount() == 0U)
        {
            SetStatus(L"Marque um ou mais pares. A sincronizacao e sempre do S_ para o C_.");
        }
        else
        {
            SetStatus(
                std::to_wstring(_manager.LinkCount()) +
                (_manager.LinkCount() == 1U ? L" vinculo ativo." : L" vinculos ativos."));
        }
    }

    void MirrorLinkDialog::ResizeColumns()
    {
        if (_list == nullptr)
        {
            return;
        }
        RECT client{};
        GetClientRect(_list, &client);
        const int total = std::max(180, static_cast<int>(client.right - client.left - 5));
        const int stateWidth = 78;
        const int firstWidth = (total - stateWidth) / 2;
        const int secondWidth = total - stateWidth - firstWidth;
        ListView_SetColumnWidth(_list, 0, firstWidth);
        ListView_SetColumnWidth(_list, 1, secondWidth);
        ListView_SetColumnWidth(_list, 2, stateWidth);
    }

    void MirrorLinkDialog::FitWindowToRows()
    {
        if (_dialog == nullptr || _list == nullptr)
        {
            return;
        }

        const int visibleRows = std::clamp(static_cast<int>(_pairs.size()), 2, 7);
        RECT itemRect{};
        int rowHeight = 20;
        if (ListView_GetItemRect(_list, 0, &itemRect, LVIR_BOUNDS) != FALSE)
        {
            rowHeight = std::max(18, static_cast<int>(itemRect.bottom - itemRect.top));
        }

        RECT windowRect{};
        RECT listRect{};
        RECT statusRect{};
        GetWindowRect(_dialog, &windowRect);
        GetWindowRect(_list, &listRect);
        GetWindowRect(_status, &statusRect);
        MapWindowPoints(HWND_DESKTOP, _dialog, reinterpret_cast<POINT*>(&listRect), 2);
        MapWindowPoints(HWND_DESKTOP, _dialog, reinterpret_cast<POINT*>(&statusRect), 2);

        const int headerHeight = 24;
        const int listHeight = headerHeight + visibleRows * rowHeight + 4;
        SetWindowPos(
            _list,
            nullptr,
            listRect.left,
            listRect.top,
            listRect.right - listRect.left,
            listHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);

        const int newStatusTop = listRect.top + listHeight + 7;
        SetWindowPos(
            _status,
            nullptr,
            statusRect.left,
            newStatusTop,
            statusRect.right - statusRect.left,
            statusRect.bottom - statusRect.top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        RECT client{};
        GetClientRect(_dialog, &client);
        const int desiredClientHeight = newStatusTop + (statusRect.bottom - statusRect.top) + 8;
        RECT desired{0, 0, client.right - client.left, desiredClientHeight};
        AdjustWindowRectEx(
            &desired,
            static_cast<DWORD>(GetWindowLongPtrW(_dialog, GWL_STYLE)),
            FALSE,
            static_cast<DWORD>(GetWindowLongPtrW(_dialog, GWL_EXSTYLE)));
        SetWindowPos(
            _dialog,
            nullptr,
            0,
            0,
            windowRect.right - windowRect.left,
            desired.bottom - desired.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void MirrorLinkDialog::HandleCheckChanged(int index, bool checked)
    {
        if (index < 0 || index >= static_cast<int>(_pairs.size()))
        {
            ReloadPairs();
            return;
        }

        const MirrorPairCandidate pair = _pairs[static_cast<std::size_t>(index)];
        if (checked)
        {
            if (pair.conflict)
            {
                SetItemCheck(index, false);
                SetStatus(L"Um dos arquivos desse par ja participa de outro vinculo.", true);
                return;
            }

            SetStatus(L"Validando encoding e sincronizando o par...");
            UpdateWindow(_status);
            std::wstring error;
            if (!_manager.LinkPair(pair.sourceBufferId, pair.mirrorBufferId, error))
            {
                SetItemCheck(index, false);
                SetStatus(error.empty() ? L"Nao foi possivel criar o vinculo." : error, true);
                return;
            }
            ReloadPairs();
            SetStatus(pair.sourceName + L" e " + pair.mirrorName + L" vinculados.");
            return;
        }

        if (pair.linked || _manager.IsPairLinked(pair.sourceBufferId, pair.mirrorBufferId))
        {
            _manager.UnlinkPair(pair.sourceBufferId, pair.mirrorBufferId);
            ReloadPairs();
            SetStatus(L"Vinculo removido.");
        }
    }

    void MirrorLinkDialog::SetItemCheck(int index, bool checked)
    {
        if (_list == nullptr || index < 0)
        {
            return;
        }
        _updatingChecks = true;
        ListView_SetCheckState(_list, index, checked ? TRUE : FALSE);
        _updatingChecks = false;
    }

    void MirrorLinkDialog::SetStatus(const std::wstring& text, bool error)
    {
        _statusIsError = error;
        if (_status != nullptr)
        {
            SetWindowTextW(_status, text.c_str());
            InvalidateRect(_status, nullptr, TRUE);
        }
    }

    void MirrorLinkDialog::ApplyTheme()
    {
        if (_dialog != nullptr && _nppData._nppHandle != nullptr)
        {
            SendMessageW(
                _nppData._nppHandle,
                NPPM_DARKMODESUBCLASSANDTHEME,
                static_cast<WPARAM>(NppDarkMode::dmfInit),
                reinterpret_cast<LPARAM>(_dialog));
        }
    }
}
