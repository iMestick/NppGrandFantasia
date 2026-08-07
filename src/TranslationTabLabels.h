#pragma once

#include "npp/PluginInterface.h"

#include <array>
#include <string>
#include <vector>

namespace NppGrandFantasia
{
    // Acrescenta apenas um rotulo visual nas abas de arquivos T_*.ini.
    // O nome/caminho real do arquivo nunca e alterado.
    class TranslationTabLabels
    {
    public:
        explicit TranslationTabLabels(NppData nppData);

        void Refresh() const;

    private:
        struct TabCandidate
        {
            HWND window = nullptr;
            int itemCount = 0;
            int selectedIndex = -1;
            RECT screenRect{};
        };

        static BOOL CALLBACK EnumerateTabControls(HWND window, LPARAM lParam);

        std::vector<TabCandidate> FindDocumentTabControls() const;
        std::array<HWND, 2> MatchTabsToViews(const std::vector<TabCandidate>& candidates) const;
        long long ScoreCandidateForView(const TabCandidate& candidate, int view) const;
        void RefreshView(int view, HWND tabControl) const;

        std::wstring GetPathFromBufferId(UINT_PTR bufferId) const;
        static bool BuildTranslationLabel(const std::wstring& path, std::wstring& label);
        static bool IsTranslationFileName(const std::wstring& fileName);
        static bool TryGetLanguageFromParentFolder(const std::wstring& path, std::wstring& language);
        static std::wstring FileNameFromPath(const std::wstring& path);
        static bool ReadTabText(HWND tabControl, int index, std::wstring& text);
        static void SetTabTextIfChanged(HWND tabControl, int index, const std::wstring& text);

        NppData _nppData{};
    };
}
