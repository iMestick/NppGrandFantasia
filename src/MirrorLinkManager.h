#pragma once

#include "MirrorToolbar.h"
#include "npp/PluginInterface.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace NppGrandFantasia
{
    constexpr UINT MirrorSyncResultMessage = WM_APP + 0x49;
    constexpr UINT MirrorDeferredSaveMessage = WM_APP + 0x4A;
    constexpr UINT MirrorDeferredDirtyMessage = WM_APP + 0x4B;
    constexpr UINT_PTR MirrorSyncTimerId = 0x4E4748U;

    struct MirrorDocumentEncoding
    {
        int unicodeMode = -1;
        int saveCodePage = -1;
        UINT internalCodePage = 0;

        bool operator==(const MirrorDocumentEncoding& other) const
        {
            return unicodeMode == other.unicodeMode &&
                saveCodePage == other.saveCodePage &&
                internalCodePage == other.internalCodePage;
        }

        bool operator!=(const MirrorDocumentEncoding& other) const
        {
            return !(*this == other);
        }
    };

    struct MirrorSyncWorkerResult
    {
        std::uint64_t generation = 0;
        UINT_PTR sourceBufferId = 0;
        UINT_PTR mirrorBufferId = 0;
        bool success = false;
        bool different = false;
        std::string targetBytes;
        std::wstring error;
        std::size_t sourceCharacters = 0;
        double conversionMilliseconds = 0.0;
    };

    struct MirrorPairCandidate
    {
        UINT_PTR sourceBufferId = 0;
        UINT_PTR mirrorBufferId = 0;
        std::wstring sourcePath;
        std::wstring mirrorPath;
        std::wstring sourceName;
        std::wstring mirrorName;
        std::wstring sourceDirectory;
        std::wstring mirrorDirectory;
        bool linked = false;
        bool conflict = false;
    };

    class MirrorSyncWorker
    {
    public:
        explicit MirrorSyncWorker(HWND resultWindow);
        ~MirrorSyncWorker();

        MirrorSyncWorker(const MirrorSyncWorker&) = delete;
        MirrorSyncWorker& operator=(const MirrorSyncWorker&) = delete;

        struct Job
        {
            std::uint64_t generation = 0;
            UINT_PTR sourceBufferId = 0;
            UINT_PTR mirrorBufferId = 0;
            std::string sourceUtf8;
            std::string mirrorUtf8;
            MirrorDocumentEncoding mirrorEncoding{};
        };

        void Submit(Job job);
        void Stop();
        static MirrorSyncWorkerResult Convert(const Job& job);

    private:
        void ThreadMain();

        HWND _resultWindow = nullptr;
        std::thread _thread;
        std::mutex _mutex;
        std::condition_variable _condition;
        std::deque<Job> _pendingJobs;
        bool _stopping = false;
    };

    class MirrorLinkManager
    {
    public:
        MirrorLinkManager(NppData nppData, HWND ownerWindow, MirrorToolbar* toolbar);
        ~MirrorLinkManager();

        MirrorLinkManager(const MirrorLinkManager&) = delete;
        MirrorLinkManager& operator=(const MirrorLinkManager&) = delete;

        void Initialize();
        void Shutdown();
        void ToggleLinkFromCurrent();
        std::vector<MirrorPairCandidate> GetAvailablePairs() const;
        bool LinkPair(UINT_PTR sourceBufferId, UINT_PTR mirrorBufferId, std::wstring& error);
        bool UnlinkPair(UINT_PTR sourceBufferId, UINT_PTR mirrorBufferId);
        void UnlinkByUser();
        void ManualSync();
        void ScheduleFromModification(UINT_PTR modifiedBufferId);
        void HandleTimer(UINT_PTR timerId);
        void HandleWorkerResult(std::unique_ptr<MirrorSyncWorkerResult> result);
        void HandleBeforeSave(UINT_PTR bufferId);
        void HandleFileSaved(UINT_PTR bufferId);
        void HandleDeferredMirrorSave();
        void HandleDeferredMirrorDirty(UINT_PTR mirrorBufferId);
        void HandleFileBeforeClose(UINT_PTR bufferId);
        void HandleFileClosed(UINT_PTR bufferId);
        void HandleFilePathChanged(UINT_PTR bufferId, const wchar_t* reason);
        void HandleBufferActivated();
        void HandleReadOnlyChanged(UINT_PTR bufferId);
        void HandleReadOnlyModifyAttempt(UINT_PTR bufferId);
        void ApplyTheme();

        static LRESULT CALLBACK NotepadSubclassProc(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam,
            UINT_PTR subclassId,
            DWORD_PTR referenceData);

        bool IsApplyingMirrorUpdate() const;
        bool IsCapturingDocuments() const;
        bool IsLinked() const;
        bool IsPairLinked(UINT_PTR sourceBufferId, UINT_PTR mirrorBufferId) const;
        std::size_t LinkCount() const;
        UINT_PTR SourceBufferId() const;
        UINT_PTR MirrorBufferId() const;

    private:
        struct OpenDocument
        {
            UINT_PTR bufferId = 0;
            int view = 0;
            int index = -1;
            std::wstring path;
            std::wstring normalizedPath;
            bool viewVisible = false;
        };

        struct Selection
        {
            Sci_Position caret = 0;
            Sci_Position anchor = 0;
        };

        struct ViewState
        {
            HWND scintilla = nullptr;
            std::vector<Selection> selections;
            int mainSelection = 0;
            Sci_Position firstVisibleLine = 0;
            int xOffset = 0;
            int zoom = 0;
        };

        struct LinkState
        {
            UINT_PTR sourceBufferId = 0;
            UINT_PTR mirrorBufferId = 0;
            std::wstring sourcePath;
            std::wstring mirrorPath;
            std::wstring sourceNormalizedPath;
            std::wstring mirrorNormalizedPath;
            MirrorDocumentEncoding sourceEncoding{};
            MirrorDocumentEncoding mirrorEncoding{};
            bool mirrorWasReadOnly = false;
            sptr_t sourceDocument = 0;
            sptr_t mirrorDocument = 0;
            std::uint64_t generation = 0;
            bool sourceSaveSyncFailed = false;
            bool deferredSaveQueued = false;
            // SCI_SETTEXT altera o documento compartilhado, mas o Notepad++
            // tambem precisa registrar o buffer como nao salvo para que Ctrl+S
            // e NPPM_SAVEFILE persistam a versao mais recente.
            bool mirrorDirtyRegisteredWithNotepad = false;
            std::wstring lastError;
        };

        bool CreateHiddenScintillas();
        void DestroyHiddenScintillas();
        void KeepInternalAccessorsHidden() const;
        std::vector<OpenDocument> EnumerateOpenDocuments() const;
        std::optional<OpenDocument> FindDocument(UINT_PTR bufferId) const;
        bool ResolvePairFromCurrent(OpenDocument& source, OpenDocument& mirror, std::wstring& error) const;
        bool CreateLink(const OpenDocument& source, const OpenDocument& mirror, std::wstring& error);
        bool CaptureDocumentPointers(
            LinkState& link,
            const OpenDocument& source,
            const OpenDocument& mirror,
            std::wstring& error);
        bool CaptureDocumentPointer(
            const OpenDocument& document,
            sptr_t& documentPointer,
            bool* visibleReadOnly,
            MirrorDocumentEncoding& encoding,
            std::wstring& error);
        bool AttachAccessors(const LinkState& link, std::wstring& error) const;
        void DetachHiddenAccessors() const;
        void ReleaseLinkDocuments(LinkState& link);
        bool ValidateLink(const LinkState& link, std::wstring& error, bool requireFilesOnDisk = true) const;
        bool SynchronizeImmediate(
            LinkState& link,
            const wchar_t* reason,
            bool showError,
            std::wstring* errorOut = nullptr,
            bool registerMirrorDirty = true);
        void SubmitBackgroundSync(LinkState& link);
        bool ApplyConvertedMirrorText(
            LinkState& link,
            const std::string& targetBytes,
            std::size_t sourceCharacters,
            double conversionMilliseconds,
            std::wstring* errorOut = nullptr,
            bool registerMirrorDirty = true);
        bool MarkMirrorDirtyThroughNotepad(LinkState& link, std::wstring& error);
        bool SaveBufferThroughNotepad(LinkState& link, UINT_PTR bufferId, std::wstring& error);
        bool SavePair(LinkState& link, std::wstring& error);
        void QueueMirrorSave(LinkState& link);
        void UnlinkAt(std::size_t index, const std::wstring& reason, bool errorState, bool showError);
        void UnlinkForBuffer(UINT_PTR bufferId, const std::wstring& reason, bool errorState, bool showError);
        void SetMirrorReadOnly(const LinkState& link, bool readOnly) const;
        ViewState CaptureViewState(HWND scintilla) const;
        std::vector<ViewState> CaptureActiveViewStates() const;
        std::vector<ViewState> CaptureVisibleMirrorStates(const LinkState& link) const;
        void RestoreVisibleMirrorStates(const std::vector<ViewState>& states) const;
        UINT_PTR GetActiveBufferInView(int view) const;
        bool IsViewVisible(int view) const;
        HWND ScintillaForView(int view) const;
        int GetBufferPosition(UINT_PTR bufferId, int priorityView = 0) const;
        bool ActivateBuffer(UINT_PTR bufferId) const;
        bool ActivateBufferInView(UINT_PTR bufferId, int priorityView) const;
        std::wstring GetPathFromBufferId(UINT_PTR bufferId) const;
        bool GetDocumentUtf8(HWND scintilla, std::string& text, std::wstring& error) const;
        MirrorDocumentEncoding CaptureActiveDocumentEncoding(HWND scintilla, UINT_PTR bufferId) const;
        int GetCheckedCustomEncodingCodePage() const;
        LinkState* FindLinkBySource(UINT_PTR bufferId);
        const LinkState* FindLinkBySource(UINT_PTR bufferId) const;
        LinkState* FindLinkByMirror(UINT_PTR bufferId);
        const LinkState* FindLinkByMirror(UINT_PTR bufferId) const;
        LinkState* FindLinkByPair(UINT_PTR sourceBufferId, UINT_PTR mirrorBufferId);
        const LinkState* FindLinkByPair(UINT_PTR sourceBufferId, UINT_PTR mirrorBufferId) const;
        bool BufferParticipatesInLink(UINT_PTR bufferId, const LinkState* except = nullptr) const;
        void RestoreOpenDocuments(
            const std::array<UINT_PTR, 2>& viewBuffers,
            UINT_PTR currentBuffer,
            int currentView,
            HWND focus) const;
        void UpdateToolbarSummary(MirrorToolbarState preferredState = MirrorToolbarState::Linked, const std::wstring& detail = {});
        static std::wstring NormalizePath(const std::wstring& path);
        static std::wstring BaseName(const std::wstring& path);
        static std::wstring DirectoryName(const std::wstring& path);
        static bool IsServerName(const std::wstring& name);
        static bool IsClientName(const std::wstring& name);
        static bool NamesMatch(const std::wstring& serverName, const std::wstring& clientName);
        static std::wstring EncodingName(const MirrorDocumentEncoding& encoding);
        void UpdateToolbar(MirrorToolbarState state, const std::wstring& detail);
        void ShowErrorOnce(const std::wstring& message);
        void HandleEncodingCommandAfter(UINT commandId, UINT_PTR bufferId);
        static bool IsEncodingCommand(UINT commandId);
        void Log(const std::wstring& message) const;
        std::wstring LogFilePath() const;

        NppData _nppData{};
        HWND _ownerWindow = nullptr;
        MirrorToolbar* _toolbar = nullptr;
        HWND _accessorHost = nullptr;
        HWND _sourceAccessor = nullptr;
        HWND _mirrorAccessor = nullptr;
        sptr_t _sourceDefaultDocument = 0;
        sptr_t _mirrorDefaultDocument = 0;
        std::unique_ptr<MirrorSyncWorker> _worker;
        std::vector<std::unique_ptr<LinkState>> _links;
        std::unordered_set<UINT_PTR> _dirtySources;
        std::deque<UINT_PTR> _deferredMirrorSaves;
        std::unordered_set<UINT_PTR> _internalSaveBuffers;
        std::atomic<std::uint64_t> _nextGeneration{0};
        bool _initialized = false;
        bool _isApplyingMirrorUpdate = false;
        bool _isCapturingDocuments = false;
        bool _errorMessageShown = false;
        bool _notepadSubclassInstalled = false;
    };
}
