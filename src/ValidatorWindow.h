#pragma once

#include "PipeColorSettings.h"
#include "PipeValidator.h"
#include "MirrorLinkManager.h"
#include "MirrorToolbar.h"
#include "FlagToolWindow.h"
#include "ToolbarStatus.h"
#include "npp/Docking.h"
#include "npp/PluginInterface.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace NppGrandFantasia
{
    struct ValidationWorkerResult
    {
        PipeValidationResult validation;
        std::uint64_t generation = 0;
        UINT_PTR bufferId = 0;
    };

    class ValidationWorker
    {
    public:
        explicit ValidationWorker(HWND resultWindow);
        ~ValidationWorker();

        ValidationWorker(const ValidationWorker&) = delete;
        ValidationWorker& operator=(const ValidationWorker&) = delete;

        void Submit(
            std::string text,
            std::string fileName,
            std::uint64_t generation,
            UINT_PTR bufferId);
        void Stop();

    private:
        struct Job
        {
            std::string text;
            std::string fileName;
            std::uint64_t generation = 0;
            UINT_PTR bufferId = 0;
        };

        void ThreadMain();

        HWND _resultWindow = nullptr;
        std::thread _thread;
        std::mutex _mutex;
        std::condition_variable _condition;
        std::optional<Job> _pendingJob;
        bool _stopping = false;
    };

    class ValidatorWindow
    {
    public:
        ValidatorWindow(HINSTANCE instance, NppData nppData, int menuCommandId);
        ~ValidatorWindow();

        ValidatorWindow(const ValidatorWindow&) = delete;
        ValidatorWindow& operator=(const ValidatorWindow&) = delete;

        bool Create();
        void Destroy();
        void Toggle();
        void Show();
        void Hide();
        bool IsVisible() const;
        void ScheduleValidation(bool immediate = false);
        void ApplyDarkMode(bool initial);
        void RefreshVisiblePipeColors(bool clearDocument = false);
        void UpdateCompactStatus();
        void HandleFileBeforeSave(UINT_PTR bufferId);
        void HandleFileSaved(UINT_PTR bufferId);
        void HandleDeferredMirrorSave();
        void HandleFileBeforeClose(UINT_PTR bufferId);
        void HandleFileClosed(UINT_PTR bufferId);
        void HandleFilePathChanged(UINT_PTR bufferId, const wchar_t* reason);
        void HandleBufferActivated();
        void HandleReadOnlyChanged(UINT_PTR bufferId);
        void HandleGlobalModified(UINT_PTR bufferId);
        void HandleScintillaModified(UINT_PTR bufferId);
        void HandleReadOnlyModifyAttempt(UINT_PTR bufferId);
        bool IsApplyingMirrorUpdate() const;
        bool IsCapturingMirrorDocuments() const;

        static INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);

    private:
        INT_PTR HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        void InitializeControls();
        void RegisterDockingPanel();
        void ResizeControls(int clientWidth, int clientHeight);
        void StartValidation();
        void DisplayResult(ValidationWorkerResult& result);
        void ClearList();
        void AddErrorRow(const PipeRecordError& error);
        void NavigateToSelectedError();
        void NavigateToLine(std::size_t oneBasedLine);
        void OpenPipeColorDialog();
        void CreateCompactToolbar();
        void CreateMirrorToolbar();
        void OpenMirrorLinkDialog();
        void OpenFlagTool();
        void EnsureCompactToolbarsStable();
        void SetCompactToolbarMessage(const std::wstring& text, bool active, bool hasError);

        void AllocateEditorVisuals();
        void ConfigureEditorVisuals(HWND scintilla);
        void ConfigureAllEditorVisuals();
        void ClearPipeIndicators(HWND scintilla);
        void ClearTaggedTextIndicators(HWND scintilla);
        void ApplyVisibleTaggedTextColors(
            HWND scintilla,
            Sci_Position firstDocumentLine,
            Sci_Position lastDocumentLine);
        void ClearBrokenTextIndicators(HWND scintilla);
        void ApplyBrokenTextIndicators(HWND scintilla, const PipeValidationResult& result);

        HWND GetCurrentScintilla() const;
        UINT_PTR GetCurrentBufferId() const;
        std::string GetCurrentDocumentText(HWND scintilla) const;
        std::string GetCurrentFileName() const;
        void SetStatus(const std::wstring& text);
        void UpdateMenuCheck(bool checked) const;

        HINSTANCE _instance = nullptr;
        NppData _nppData{};
        int _menuCommandId = 0;
        HWND _dialog = nullptr;
        HWND _statusText = nullptr;
        HWND _errorList = nullptr;
        HWND _validateButton = nullptr;
        HWND _colorsButton = nullptr;
        HWND _autoValidateCheck = nullptr;
        DockedWidgetData _dockData{};
        bool _dockingRegistered = false;
        bool _modelessRegistered = false;
        std::unique_ptr<ValidationWorker> _worker;
        std::atomic<std::uint64_t> _generation{0};
        UINT_PTR _displayedBufferId = 0;
        bool _currentValidationActive = false;
        PipeValidationResult _currentResult{};
        ToolbarStatus _compactToolbar{};
        MirrorToolbar _mirrorToolbar{};
        FlagToolWindow _flagToolWindow{};
        std::unique_ptr<MirrorLinkManager> _mirrorLinkManager;
        PipeColorSettings _pipeColorSettings{};
        bool _settingsLoaded = false;
        int _taggedTextIndicator = -1;
        int _firstPipeIndicator = -1;
        int _validIdIndicator = -1;
        int _brokenTextIndicator = -1;
    };
}
