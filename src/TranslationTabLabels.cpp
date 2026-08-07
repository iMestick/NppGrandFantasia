#include "TranslationTabLabels.h"

#include "npp/Notepad_plus_msgs.h"

#include <algorithm>
#include <array>
#include <commctrl.h>
#include <cwchar>
#include <cwctype>
#include <cstdlib>
#include <iterator>
#include <limits>

namespace NppGrandFantasia
{
    namespace
    {
        constexpr wchar_t DocumentTabWindowText[] = L"Tab";
        constexpr wchar_t TranslateFolderPrefix[] = L"Translate_";
        constexpr std::size_t MaxLanguageCodeLength = 16U;

        long long Square(long long value)
        {
            return value * value;
        }

        long long CenterDistanceSquared(const RECT& first, const RECT& second)
        {
            const long long firstX = static_cast<long long>(first.left) + first.right;
            const long long firstY = static_cast<long long>(first.top) + first.bottom;
            const long long secondX = static_cast<long long>(second.left) + second.right;
            const long long secondY = static_cast<long long>(second.top) + second.bottom;
            return Square(firstX - secondX) + Square(firstY - secondY);
        }

        bool StartsWithNoCase(const std::wstring& text, const wchar_t* prefix)
        {
            if (prefix == nullptr)
            {
                return false;
            }

            const std::size_t prefixLength = std::wcslen(prefix);
            if (text.size() < prefixLength)
            {
                return false;
            }

            for (std::size_t index = 0; index < prefixLength; ++index)
            {
                if (std::towupper(text[index]) != std::towupper(prefix[index]))
                {
                    return false;
                }
            }
            return true;
        }
    }

    TranslationTabLabels::TranslationTabLabels(NppData nppData)
        : _nppData(nppData)
    {
    }

    void TranslationTabLabels::Refresh() const
    {
        if (_nppData._nppHandle == nullptr || IsWindow(_nppData._nppHandle) == FALSE)
        {
            return;
        }

        const std::vector<TabCandidate> candidates = FindDocumentTabControls();
        if (candidates.empty())
        {
            return;
        }

        const std::array<HWND, 2> tabs = MatchTabsToViews(candidates);
        for (int view = 0; view <= 1; ++view)
        {
            if (tabs[static_cast<std::size_t>(view)] != nullptr)
            {
                RefreshView(view, tabs[static_cast<std::size_t>(view)]);
            }
        }
    }

    BOOL CALLBACK TranslationTabLabels::EnumerateTabControls(HWND window, LPARAM lParam)
    {
        auto* candidates = reinterpret_cast<std::vector<TabCandidate>*>(lParam);
        if (candidates == nullptr || window == nullptr)
        {
            return FALSE;
        }

        wchar_t className[64]{};
        GetClassNameW(window, className, static_cast<int>(std::size(className)));
        if (_wcsicmp(className, WC_TABCONTROLW) != 0)
        {
            return TRUE;
        }

        // O tab bar de documentos do Notepad++ e criado como SysTabControl32,
        // texto interno "Tab" e TCS_OWNERDRAWFIXED. Esse filtro evita tocar
        // nos tab controls dos paineis dockaveis ou de outros plugins.
        wchar_t windowText[64]{};
        GetWindowTextW(window, windowText, static_cast<int>(std::size(windowText)));
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if (_wcsicmp(windowText, DocumentTabWindowText) != 0 ||
            (style & TCS_OWNERDRAWFIXED) == 0)
        {
            return TRUE;
        }

        TabCandidate candidate{};
        candidate.window = window;
        candidate.itemCount = static_cast<int>(SendMessageW(window, TCM_GETITEMCOUNT, 0, 0));
        candidate.selectedIndex = static_cast<int>(SendMessageW(window, TCM_GETCURSEL, 0, 0));
        GetWindowRect(window, &candidate.screenRect);
        candidates->push_back(candidate);
        return TRUE;
    }

    std::vector<TranslationTabLabels::TabCandidate> TranslationTabLabels::FindDocumentTabControls() const
    {
        std::vector<TabCandidate> candidates;
        EnumChildWindows(
            _nppData._nppHandle,
            EnumerateTabControls,
            reinterpret_cast<LPARAM>(&candidates));
        return candidates;
    }

    std::array<HWND, 2> TranslationTabLabels::MatchTabsToViews(
        const std::vector<TabCandidate>& candidates) const
    {
        std::array<HWND, 2> result{nullptr, nullptr};
        std::array<bool, 2> visible{false, false};

        for (int view = 0; view <= 1; ++view)
        {
            const int currentIndex = static_cast<int>(SendMessageW(
                _nppData._nppHandle,
                NPPM_GETCURRENTDOCINDEX,
                0,
                view));
            visible[static_cast<std::size_t>(view)] = currentIndex >= 0;
        }

        if (!visible[0] && !visible[1])
        {
            return result;
        }

        if (visible[0] && visible[1] && candidates.size() >= 2U)
        {
            long long bestScore = std::numeric_limits<long long>::max();
            for (std::size_t first = 0; first < candidates.size(); ++first)
            {
                for (std::size_t second = 0; second < candidates.size(); ++second)
                {
                    if (first == second)
                    {
                        continue;
                    }

                    const long long score =
                        ScoreCandidateForView(candidates[first], 0) +
                        ScoreCandidateForView(candidates[second], 1);
                    if (score < bestScore)
                    {
                        bestScore = score;
                        result[0] = candidates[first].window;
                        result[1] = candidates[second].window;
                    }
                }
            }
            return result;
        }

        const int visibleView = visible[1] ? 1 : 0;
        const TabCandidate* bestCandidate = nullptr;
        long long bestScore = std::numeric_limits<long long>::max();
        for (const TabCandidate& candidate : candidates)
        {
            const long long score = ScoreCandidateForView(candidate, visibleView);
            if (score < bestScore)
            {
                bestScore = score;
                bestCandidate = &candidate;
            }
        }

        if (bestCandidate != nullptr)
        {
            result[static_cast<std::size_t>(visibleView)] = bestCandidate->window;
        }
        return result;
    }

    long long TranslationTabLabels::ScoreCandidateForView(
        const TabCandidate& candidate,
        int view) const
    {
        const int viewType = view == 0 ? PRIMARY_VIEW : SECOND_VIEW;
        const int expectedCount = static_cast<int>(SendMessageW(
            _nppData._nppHandle,
            NPPM_GETNBOPENFILES,
            0,
            viewType));
        const int expectedSelected = static_cast<int>(SendMessageW(
            _nppData._nppHandle,
            NPPM_GETCURRENTDOCINDEX,
            0,
            view));

        long long score =
            static_cast<long long>(std::abs(candidate.itemCount - expectedCount)) * 1000000000000LL;

        if (expectedSelected >= 0 && candidate.selectedIndex != expectedSelected)
        {
            score += 10000000000LL;
        }

        const HWND scintilla = view == 0
            ? _nppData._scintillaMainHandle
            : _nppData._scintillaSecondHandle;
        if (scintilla != nullptr && IsWindow(scintilla) != FALSE)
        {
            RECT scintillaRect{};
            if (GetWindowRect(scintilla, &scintillaRect) != FALSE)
            {
                score += CenterDistanceSquared(candidate.screenRect, scintillaRect);
            }

            if (GetParent(candidate.window) == GetParent(scintilla))
            {
                score -= 1000000LL;
            }
        }

        return score;
    }

    void TranslationTabLabels::RefreshView(int view, HWND tabControl) const
    {
        if (tabControl == nullptr || IsWindow(tabControl) == FALSE)
        {
            return;
        }

        const int viewType = view == 0 ? PRIMARY_VIEW : SECOND_VIEW;
        const int bufferCount = static_cast<int>(SendMessageW(
            _nppData._nppHandle,
            NPPM_GETNBOPENFILES,
            0,
            viewType));
        const int tabCount = static_cast<int>(SendMessageW(tabControl, TCM_GETITEMCOUNT, 0, 0));
        const int count = std::min(bufferCount, tabCount);

        for (int index = 0; index < count; ++index)
        {
            const UINT_PTR bufferId = static_cast<UINT_PTR>(SendMessageW(
                _nppData._nppHandle,
                NPPM_GETBUFFERIDFROMPOS,
                index,
                view));
            if (bufferId == 0)
            {
                continue;
            }

            const std::wstring path = GetPathFromBufferId(bufferId);
            std::wstring label;
            if (!BuildTranslationLabel(path, label))
            {
                continue;
            }

            SetTabTextIfChanged(tabControl, index, label);
        }
    }

    std::wstring TranslationTabLabels::GetPathFromBufferId(UINT_PTR bufferId) const
    {
        const int length = static_cast<int>(SendMessageW(
            _nppData._nppHandle,
            NPPM_GETFULLPATHFROMBUFFERID,
            bufferId,
            0));
        if (length <= 0)
        {
            return {};
        }

        std::wstring path(static_cast<std::size_t>(length) + 1U, L'\0');
        const int copied = static_cast<int>(SendMessageW(
            _nppData._nppHandle,
            NPPM_GETFULLPATHFROMBUFFERID,
            bufferId,
            reinterpret_cast<LPARAM>(path.data())));
        if (copied <= 0)
        {
            return {};
        }

        path.resize(static_cast<std::size_t>(copied));
        return path;
    }

    bool TranslationTabLabels::BuildTranslationLabel(
        const std::wstring& path,
        std::wstring& label)
    {
        label.clear();
        const std::wstring fileName = FileNameFromPath(path);
        if (!IsTranslationFileName(fileName))
        {
            return false;
        }

        std::wstring language;
        if (!TryGetLanguageFromParentFolder(path, language))
        {
            return false;
        }

        label.reserve(fileName.size() + language.size() + 3U);
        label = fileName;
        label += L" [";
        label += language;
        label += L"]";
        return true;
    }

    bool TranslationTabLabels::IsTranslationFileName(const std::wstring& fileName)
    {
        if (fileName.size() < 7U ||
            std::towupper(fileName[0]) != L'T' ||
            fileName[1] != L'_')
        {
            return false;
        }

        constexpr wchar_t IniExtension[] = L".ini";
        constexpr std::size_t extensionLength = 4U;
        if (fileName.size() <= extensionLength)
        {
            return false;
        }

        const std::size_t extensionStart = fileName.size() - extensionLength;
        for (std::size_t index = 0; index < extensionLength; ++index)
        {
            if (std::towupper(fileName[extensionStart + index]) !=
                std::towupper(IniExtension[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool TranslationTabLabels::TryGetLanguageFromParentFolder(
        const std::wstring& path,
        std::wstring& language)
    {
        language.clear();
        const std::size_t fileSeparator = path.find_last_of(L"\\/");
        if (fileSeparator == std::wstring::npos || fileSeparator == 0U)
        {
            return false;
        }

        const std::size_t parentSeparator = path.find_last_of(L"\\/", fileSeparator - 1U);
        const std::size_t parentStart =
            parentSeparator == std::wstring::npos ? 0U : parentSeparator + 1U;
        const std::wstring parent = path.substr(parentStart, fileSeparator - parentStart);

        if (!StartsWithNoCase(parent, TranslateFolderPrefix))
        {
            return false;
        }

        const std::size_t prefixLength = std::wcslen(TranslateFolderPrefix);
        if (parent.size() <= prefixLength)
        {
            return false;
        }

        language = parent.substr(prefixLength);
        if (language.empty() || language.size() > MaxLanguageCodeLength)
        {
            language.clear();
            return false;
        }

        for (wchar_t& character : language)
        {
            if (std::iswalnum(character) == 0 && character != L'-')
            {
                language.clear();
                return false;
            }
            character = static_cast<wchar_t>(std::towupper(character));
        }
        return true;
    }

    std::wstring TranslationTabLabels::FileNameFromPath(const std::wstring& path)
    {
        const std::size_t separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            return path;
        }
        if (separator + 1U >= path.size())
        {
            return {};
        }
        return path.substr(separator + 1U);
    }

    bool TranslationTabLabels::ReadTabText(
        HWND tabControl,
        int index,
        std::wstring& text)
    {
        text.clear();
        if (tabControl == nullptr || index < 0)
        {
            return false;
        }

        std::array<wchar_t, 1024> buffer{};
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = buffer.data();
        item.cchTextMax = static_cast<int>(buffer.size());
        if (SendMessageW(
                tabControl,
                TCM_GETITEMW,
                static_cast<WPARAM>(index),
                reinterpret_cast<LPARAM>(&item)) == FALSE)
        {
            return false;
        }

        text.assign(buffer.data());
        return true;
    }

    void TranslationTabLabels::SetTabTextIfChanged(
        HWND tabControl,
        int index,
        const std::wstring& text)
    {
        std::wstring currentText;
        if (ReadTabText(tabControl, index, currentText) && currentText == text)
        {
            return;
        }

        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(text.c_str());
        SendMessageW(
            tabControl,
            TCM_SETITEMW,
            static_cast<WPARAM>(index),
            reinterpret_cast<LPARAM>(&item));
    }
}
