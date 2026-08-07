#include "ValidatorWindow.h"

#include "PipeColorDialog.h"
#include "GrandFantasiaTextColors.h"
#include "MirrorLinkDialog.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <commctrl.h>
#include <cctype>
#include <cwchar>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace NppGrandFantasia
{
    namespace
    {
        constexpr UINT_PTR ValidationTimerId = 0x4E4746;
        constexpr UINT ValidationDelayMs = 300;
        constexpr UINT WorkerResultMessage = WM_APP + 0x47;
        constexpr LRESULT MaxDocumentLength = 512LL * 1024LL * 1024LL;
        constexpr wchar_t DockTitle[] = L"NppGrandFantasia";
        constexpr wchar_t PluginModuleName[] = L"NppGrandFantasia.dll";

        std::wstring ToWideNumber(std::size_t value)
        {
            return std::to_wstring(static_cast<unsigned long long>(value));
        }

        std::wstring FormatLineRange(std::size_t startLine, std::size_t endLine)
        {
            if (startLine == endLine)
            {
                return ToWideNumber(startLine);
            }

            return ToWideNumber(startLine) + L"-" + ToWideNumber(endLine);
        }

        std::wstring BuildDifferenceText(const PipeRecordError& error)
        {
            if (error.actualPipes < error.expectedPipes)
            {
                return L"-" + ToWideNumber(error.expectedPipes - error.actualPipes);
            }

            return L"+" + ToWideNumber(error.actualPipes - error.expectedPipes);
        }

        bool ReadScintillaLine(
            HWND scintilla,
            Sci_Position documentLine,
            std::string& line,
            Sci_Position& lineStart)
        {
            const LRESULT rawLength = SendMessageW(
                scintilla,
                SCI_LINELENGTH,
                static_cast<WPARAM>(documentLine),
                0);
            if (rawLength <= 0)
            {
                line.clear();
                lineStart = static_cast<Sci_Position>(SendMessageW(
                    scintilla,
                    SCI_POSITIONFROMLINE,
                    static_cast<WPARAM>(documentLine),
                    0));
                return lineStart >= 0;
            }

            line.assign(static_cast<std::size_t>(rawLength) + 1U, '\0');
            const LRESULT copied = SendMessageW(
                scintilla,
                SCI_GETLINE,
                static_cast<WPARAM>(documentLine),
                reinterpret_cast<LPARAM>(line.data()));
            if (copied < 0)
            {
                return false;
            }

            line.resize(static_cast<std::size_t>(copied));
            lineStart = static_cast<Sci_Position>(SendMessageW(
                scintilla,
                SCI_POSITIONFROMLINE,
                static_cast<WPARAM>(documentLine),
                0));
            return lineStart >= 0;
        }

        std::size_t GetLineContentLength(const std::string& line)
        {
            std::size_t length = line.size();
            while (length > 0U && (line[length - 1U] == '\r' || line[length - 1U] == '\n'))
            {
                --length;
            }
            return length;
        }

        std::pair<std::size_t, std::size_t> GetRecordIdRange(
            const std::string& line,
            bool isRecordStartLine)
        {
            if (!isRecordStartLine)
            {
                return {0U, 0U};
            }

            std::size_t idStart = 0;
            if (line.size() >= 3U &&
                static_cast<unsigned char>(line[0]) == 0xEFU &&
                static_cast<unsigned char>(line[1]) == 0xBBU &&
                static_cast<unsigned char>(line[2]) == 0xBFU)
            {
                idStart = 3U;
            }

            std::size_t idEnd = idStart;
            while (idEnd < line.size() &&
                   std::isdigit(static_cast<unsigned char>(line[idEnd])) != 0)
            {
                ++idEnd;
            }

            if (idEnd <= idStart || idEnd >= line.size() || line[idEnd] != '|')
            {
                return {0U, 0U};
            }

            return {idStart, idEnd};
        }

        void FillTaggedTextRange(
            HWND scintilla,
            int indicator,
            Sci_Position lineStart,
            const std::string& line,
            std::size_t begin,
            std::size_t end,
            PackedRgb color,
            std::pair<std::size_t, std::size_t> idRange)
        {
            if (indicator < 0 || begin >= end || end > line.size())
            {
                return;
            }

            SendMessageW(
                scintilla,
                SCI_SETINDICATORCURRENT,
                static_cast<WPARAM>(indicator),
                0);
            SendMessageW(
                scintilla,
                SCI_SETINDICATORVALUE,
                static_cast<WPARAM>(SC_INDICVALUEBIT | (color & 0x00FFFFFFU)),
                0);

            std::size_t rangeStart = begin;
            auto isReservedCharacter = [&](std::size_t index)
            {
                return line[index] == '|' ||
                       (index >= idRange.first && index < idRange.second);
            };

            for (std::size_t index = begin; index < end; ++index)
            {
                if (!isReservedCharacter(index))
                {
                    continue;
                }

                if (index > rangeStart)
                {
                    SendMessageW(
                        scintilla,
                        SCI_INDICATORFILLRANGE,
                        static_cast<WPARAM>(lineStart + static_cast<Sci_Position>(rangeStart)),
                        static_cast<LPARAM>(index - rangeStart));
                }
                rangeStart = index + 1U;
            }

            if (end > rangeStart)
            {
                SendMessageW(
                    scintilla,
                    SCI_INDICATORFILLRANGE,
                    static_cast<WPARAM>(lineStart + static_cast<Sci_Position>(rangeStart)),
                    static_cast<LPARAM>(end - rangeStart));
            }
        }
    }

    ValidationWorker::ValidationWorker(HWND resultWindow)
        : _resultWindow(resultWindow),
          _thread(&ValidationWorker::ThreadMain, this)
    {
    }

    ValidationWorker::~ValidationWorker()
    {
        Stop();
    }

    void ValidationWorker::Submit(
        std::string text,
        std::string fileName,
        std::uint64_t generation,
        UINT_PTR bufferId)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_stopping)
            {
                return;
            }

            _pendingJob = Job{std::move(text), std::move(fileName), generation, bufferId};
        }

        _condition.notify_one();
    }

    void ValidationWorker::Stop()
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_stopping)
            {
                return;
            }

            _stopping = true;
            _pendingJob.reset();
        }

        _condition.notify_one();

        if (_thread.joinable())
        {
            _thread.join();
        }
    }

    void ValidationWorker::ThreadMain()
    {
        for (;;)
        {
            Job job;

            {
                std::unique_lock<std::mutex> lock(_mutex);
                _condition.wait(lock, [this]() { return _stopping || _pendingJob.has_value(); });

                if (_stopping)
                {
                    return;
                }

                job = std::move(*_pendingJob);
                _pendingJob.reset();
            }

            auto* payload = new ValidationWorkerResult{
                AnalyzeIniText(job.text, job.fileName),
                job.generation,
                job.bufferId,
            };

            if (!PostMessageW(_resultWindow, WorkerResultMessage, 0, reinterpret_cast<LPARAM>(payload)))
            {
                delete payload;
            }
        }
    }

    ValidatorWindow::ValidatorWindow(HINSTANCE instance, NppData nppData, int menuCommandId)
        : _instance(instance),
          _nppData(nppData),
          _menuCommandId(menuCommandId)
    {
    }

    ValidatorWindow::~ValidatorWindow()
    {
        Destroy();
    }

    bool ValidatorWindow::Create()
    {
        if (_dialog != nullptr)
        {
            return true;
        }

        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_WIN95_CLASSES;
        InitCommonControlsEx(&controls);

        _dialog = CreateDialogParamW(
            _instance,
            MAKEINTRESOURCEW(IDD_PIPE_VALIDATOR),
            _nppData._nppHandle,
            DialogProc,
            reinterpret_cast<LPARAM>(this));

        if (_dialog == nullptr)
        {
            return false;
        }

        _modelessRegistered = SendMessageW(
            _nppData._nppHandle,
            NPPM_MODELESSDIALOG,
            MODELESSDIALOGADD,
            reinterpret_cast<LPARAM>(_dialog)) != 0;

        RegisterDockingPanel();
        if (!_dockingRegistered)
        {
            Destroy();
            return false;
        }

        CreateCompactToolbar();
        CreateMirrorToolbar();
        if (!_mirrorLinkManager)
        {
            _mirrorLinkManager = std::make_unique<MirrorLinkManager>(
                _nppData,
                _dialog,
                &_mirrorToolbar);
            _mirrorLinkManager->Initialize();
        }

        // O painel completo inicia oculto. Os dois blocos compactos da barra
        // permanecem visiveis e ativos durante toda a sessao.
        SendMessageW(
            _nppData._nppHandle,
            NPPM_DMMHIDE,
            0,
            reinterpret_cast<LPARAM>(_dialog));
        UpdateMenuCheck(false);
        return true;
    }

    void ValidatorWindow::Destroy()
    {
        if (_dialog == nullptr)
        {
            return;
        }

        KillTimer(_dialog, ValidationTimerId);
        KillTimer(_dialog, MirrorSyncTimerId);

        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->Shutdown();
            _mirrorLinkManager.reset();
        }
        _flagToolWindow.Destroy();
        _mirrorToolbar.Destroy();
        _compactToolbar.Destroy();

        if (_worker)
        {
            _worker->Stop();
            _worker.reset();
        }

        MSG pendingMessage{};
        while (PeekMessageW(
            &pendingMessage,
            _dialog,
            WorkerResultMessage,
            WorkerResultMessage,
            PM_REMOVE))
        {
            delete reinterpret_cast<ValidationWorkerResult*>(pendingMessage.lParam);
        }
        while (PeekMessageW(
            &pendingMessage,
            _dialog,
            MirrorSyncResultMessage,
            MirrorSyncResultMessage,
            PM_REMOVE))
        {
            delete reinterpret_cast<MirrorSyncWorkerResult*>(pendingMessage.lParam);
        }

        if (_modelessRegistered)
        {
            SendMessageW(
                _nppData._nppHandle,
                NPPM_MODELESSDIALOG,
                MODELESSDIALOGREMOVE,
                reinterpret_cast<LPARAM>(_dialog));
            _modelessRegistered = false;
        }

        DestroyWindow(_dialog);
        _dialog = nullptr;
        _dockingRegistered = false;
        UpdateMenuCheck(false);
    }

    void ValidatorWindow::Toggle()
    {
        if (!Create())
        {
            MessageBoxW(
                _nppData._nppHandle,
                L"Nao foi possivel criar o painel do Validador de Pipes.",
                L"NppGrandFantasia",
                MB_OK | MB_ICONERROR);
            return;
        }

        if (IsVisible())
        {
            Hide();
        }
        else
        {
            Show();
        }
    }

    void ValidatorWindow::Show()
    {
        if (!Create())
        {
            return;
        }

        SendMessageW(
            _nppData._nppHandle,
            NPPM_DMMSHOW,
            0,
            reinterpret_cast<LPARAM>(_dialog));
        UpdateMenuCheck(true);
        ScheduleValidation(true);
    }

    void ValidatorWindow::Hide()
    {
        if (_dialog == nullptr)
        {
            return;
        }

        SendMessageW(
            _nppData._nppHandle,
            NPPM_DMMHIDE,
            0,
            reinterpret_cast<LPARAM>(_dialog));
        UpdateMenuCheck(false);
    }

    bool ValidatorWindow::IsVisible() const
    {
        return _dialog != nullptr && IsWindowVisible(_dialog) != FALSE;
    }

    void ValidatorWindow::ScheduleValidation(bool immediate)
    {
        if (_dialog == nullptr)
        {
            return;
        }

        if (_autoValidateCheck != nullptr &&
            static_cast<LRESULT>(SendMessageW(_autoValidateCheck, BM_GETCHECK, 0, 0)) != BST_CHECKED &&
            !immediate)
        {
            return;
        }

        ++_generation;
        KillTimer(_dialog, ValidationTimerId);
        SetTimer(_dialog, ValidationTimerId, immediate ? 1U : ValidationDelayMs, nullptr);
    }

    void ValidatorWindow::ApplyDarkMode(bool initial)
    {
        if (_dialog == nullptr)
        {
            return;
        }

        // Paineis registrados no Docking Manager recebem o tema do Notepad++
        // automaticamente. Aqui atualizamos apenas os elementos proprios.
        _compactToolbar.ApplyTheme();
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->ApplyTheme();
        }
        else
        {
            _mirrorToolbar.ApplyTheme();
        }

        if (!initial)
        {
            InvalidateRect(_dialog, nullptr, TRUE);
            ConfigureAllEditorVisuals();
            RefreshVisiblePipeColors(true);
        }
    }

    void ValidatorWindow::RefreshVisiblePipeColors(bool clearDocument)
    {
        const HWND scintilla = GetCurrentScintilla();
        if (scintilla == nullptr ||
            (_taggedTextIndicator < 0 && _firstPipeIndicator < 0 && _validIdIndicator < 0))
        {
            return;
        }

        if (clearDocument)
        {
            ClearTaggedTextIndicators(scintilla);
            ClearPipeIndicators(scintilla);
        }

        if (!_currentValidationActive)
        {
            return;
        }

        const LRESULT firstVisible = SendMessageW(scintilla, SCI_GETFIRSTVISIBLELINE, 0, 0);
        const LRESULT linesOnScreen = SendMessageW(scintilla, SCI_LINESONSCREEN, 0, 0);
        const LRESULT lineCount = SendMessageW(scintilla, SCI_GETLINECOUNT, 0, 0);
        if (firstVisible < 0 || linesOnScreen < 0 || lineCount <= 0)
        {
            return;
        }

        Sci_Position previousDocumentLine = -1;
        Sci_Position firstDocumentLine = std::numeric_limits<Sci_Position>::max();
        Sci_Position lastDocumentLine = -1;
        const LRESULT lastVisible = firstVisible + linesOnScreen + 1;
        auto recordIt = _currentResult.records.begin();

        for (LRESULT visibleLine = firstVisible; visibleLine <= lastVisible; ++visibleLine)
        {
            const Sci_Position documentLine = static_cast<Sci_Position>(
                SendMessageW(scintilla, SCI_DOCLINEFROMVISIBLE, static_cast<WPARAM>(visibleLine), 0));

            if (documentLine < 0 || documentLine >= lineCount || documentLine == previousDocumentLine)
            {
                continue;
            }
            previousDocumentLine = documentLine;
            firstDocumentLine = std::min(firstDocumentLine, documentLine);
            lastDocumentLine = std::max(lastDocumentLine, documentLine);

            const LRESULT rawLength = SendMessageW(
                scintilla,
                SCI_LINELENGTH,
                static_cast<WPARAM>(documentLine),
                0);
            if (rawLength <= 0)
            {
                continue;
            }

            std::string line(static_cast<std::size_t>(rawLength) + 1U, '\0');
            const LRESULT copied = SendMessageW(
                scintilla,
                SCI_GETLINE,
                static_cast<WPARAM>(documentLine),
                reinterpret_cast<LPARAM>(line.data()));
            if (copied <= 0)
            {
                continue;
            }
            line.resize(static_cast<std::size_t>(copied));

            const Sci_Position lineStart = static_cast<Sci_Position>(SendMessageW(
                scintilla,
                SCI_POSITIONFROMLINE,
                static_cast<WPARAM>(documentLine),
                0));
            if (lineStart < 0)
            {
                continue;
            }

            const std::size_t oneBasedLine = static_cast<std::size_t>(documentLine) + 1U;
            while (recordIt != _currentResult.records.end() &&
                   recordIt->endLine < oneBasedLine)
            {
                ++recordIt;
            }

            const bool belongsToRecord =
                recordIt != _currentResult.records.end() &&
                recordIt->startLine <= oneBasedLine &&
                recordIt->endLine >= oneBasedLine;
            const bool belongsToBrokenRecord =
                belongsToRecord && !recordIt->IsValid();

            // Registros quebrados usam uma unica cor para todos os caracteres.
            // Neles nao aplicamos as cores normais de pipes nem a cor verde do ID.
            if (!belongsToBrokenRecord && _firstPipeIndicator >= 0)
            {
                std::array<std::vector<Sci_Position>, 4> positionsByColor;
                int pipeIndex = 0;
                const int colorCount = std::clamp(_pipeColorSettings.colorCount, 1, 4);

                for (std::size_t index = 0; index < line.size(); ++index)
                {
                    if (line[index] != '|')
                    {
                        continue;
                    }

                    const int colorIndex = pipeIndex % colorCount;
                    positionsByColor[static_cast<std::size_t>(colorIndex)].push_back(
                        lineStart + static_cast<Sci_Position>(index));
                    ++pipeIndex;
                }

                for (int colorIndex = 0; colorIndex < colorCount; ++colorIndex)
                {
                    SendMessageW(
                        scintilla,
                        SCI_SETINDICATORCURRENT,
                        static_cast<WPARAM>(_firstPipeIndicator + colorIndex),
                        0);

                    for (const Sci_Position position : positionsByColor[static_cast<std::size_t>(colorIndex)])
                    {
                        SendMessageW(
                            scintilla,
                            SCI_INDICATORFILLRANGE,
                            static_cast<WPARAM>(position),
                            1);
                    }
                }
            }

            if (!belongsToBrokenRecord &&
                _validIdIndicator >= 0 &&
                belongsToRecord &&
                recordIt->startLine == oneBasedLine)
            {
                std::size_t idStart = 0;
                if (line.size() >= 3U &&
                    static_cast<unsigned char>(line[0]) == 0xEFU &&
                    static_cast<unsigned char>(line[1]) == 0xBBU &&
                    static_cast<unsigned char>(line[2]) == 0xBFU)
                {
                    idStart = 3U;
                }

                std::size_t idEnd = idStart;
                while (idEnd < line.size() &&
                       std::isdigit(static_cast<unsigned char>(line[idEnd])) != 0)
                {
                    ++idEnd;
                }

                if (idEnd > idStart && idEnd < line.size() && line[idEnd] == '|')
                {
                    SendMessageW(
                        scintilla,
                        SCI_SETINDICATORCURRENT,
                        static_cast<WPARAM>(_validIdIndicator),
                        0);
                    SendMessageW(
                        scintilla,
                        SCI_INDICATORFILLRANGE,
                        static_cast<WPARAM>(lineStart + static_cast<Sci_Position>(idStart)),
                        static_cast<LPARAM>(idEnd - idStart));
                }
            }
        }

        if (firstDocumentLine <= lastDocumentLine)
        {
            ApplyVisibleTaggedTextColors(scintilla, firstDocumentLine, lastDocumentLine);
        }
    }

    void ValidatorWindow::UpdateCompactStatus()
    {
        if (!_compactToolbar.IsCreated())
        {
            CreateCompactToolbar();
        }

        if (_displayedBufferId == 0 || _displayedBufferId != GetCurrentBufferId())
        {
            SetCompactToolbarMessage(L"Validando...", false, false);
            return;
        }

        if (!_currentResult.validationActive)
        {
            if (_currentResult.headerPresent)
            {
                SetCompactToolbarMessage(L"Cabecalho invalido", false, false);
            }
            else
            {
                SetCompactToolbarMessage(L"Sem validacao", false, false);
            }
            return;
        }

        std::vector<ToolbarErrorLink> errorLines;
        errorLines.reserve(_currentResult.errors.size());
        for (const PipeRecordError& error : _currentResult.errors)
        {
            errorLines.push_back({error.startLine, error.endLine});
        }

        _compactToolbar.SetErrorLines(std::move(errorLines), true);
    }

    void ValidatorWindow::HandleFileBeforeSave(UINT_PTR bufferId)
    {
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->HandleBeforeSave(bufferId);
        }
    }

    void ValidatorWindow::HandleFileSaved(UINT_PTR bufferId)
    {
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->HandleFileSaved(bufferId);
        }
    }

    void ValidatorWindow::HandleDeferredMirrorSave()
    {
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->HandleDeferredMirrorSave();
        }
        EnsureCompactToolbarsStable();
    }

    void ValidatorWindow::HandleFileBeforeClose(UINT_PTR bufferId)
    {
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->HandleFileBeforeClose(bufferId);
        }
    }

    void ValidatorWindow::HandleFileClosed(UINT_PTR bufferId)
    {
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->HandleFileClosed(bufferId);
        }
    }

    void ValidatorWindow::HandleFilePathChanged(UINT_PTR bufferId, const wchar_t* reason)
    {
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->HandleFilePathChanged(bufferId, reason);
        }
    }

    void ValidatorWindow::HandleBufferActivated()
    {
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->HandleBufferActivated();
        }

        // Notificacoes causadas pela ativacao interna do S_/C_ nao devem
        // mexer no layout. Em uma ativacao real, apenas reafirmamos os dois
        // controles existentes, sem destruir ou recriar a barra.
        if (!IsCapturingMirrorDocuments() && !IsApplyingMirrorUpdate())
        {
            EnsureCompactToolbarsStable();
        }
    }

    void ValidatorWindow::HandleReadOnlyChanged(UINT_PTR bufferId)
    {
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->HandleReadOnlyChanged(bufferId);
        }
    }

    void ValidatorWindow::HandleGlobalModified(UINT_PTR bufferId)
    {
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->ScheduleFromModification(bufferId);
        }
    }

    void ValidatorWindow::HandleScintillaModified(UINT_PTR bufferId)
    {
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->ScheduleFromModification(bufferId);
        }
    }

    void ValidatorWindow::HandleReadOnlyModifyAttempt(UINT_PTR bufferId)
    {
        if (_mirrorLinkManager)
        {
            _mirrorLinkManager->HandleReadOnlyModifyAttempt(bufferId);
        }
    }

    bool ValidatorWindow::IsApplyingMirrorUpdate() const
    {
        return _mirrorLinkManager && _mirrorLinkManager->IsApplyingMirrorUpdate();
    }

    bool ValidatorWindow::IsCapturingMirrorDocuments() const
    {
        return _mirrorLinkManager && _mirrorLinkManager->IsCapturingDocuments();
    }

    INT_PTR CALLBACK ValidatorWindow::DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
    {
        ValidatorWindow* self = reinterpret_cast<ValidatorWindow*>(GetWindowLongPtrW(dialog, DWLP_USER));

        if (message == WM_INITDIALOG)
        {
            self = reinterpret_cast<ValidatorWindow*>(lParam);
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
            self->_dialog = dialog;
        }

        return self != nullptr ? self->HandleMessage(message, wParam, lParam) : FALSE;
    }

    INT_PTR ValidatorWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == MirrorDeferredSaveMessage)
        {
            HandleDeferredMirrorSave();
            return TRUE;
        }
        if (message == MirrorDeferredDirtyMessage)
        {
            if (_mirrorLinkManager)
            {
                _mirrorLinkManager->HandleDeferredMirrorDirty(
                    static_cast<UINT_PTR>(wParam));
            }
            EnsureCompactToolbarsStable();
            return TRUE;
        }

        switch (message)
        {
        case WM_INITDIALOG:
            InitializeControls();
            return TRUE;

        case WM_SIZE:
            ResizeControls(LOWORD(lParam), HIWORD(lParam));
            return TRUE;

        case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 225;
            info->ptMinTrackSize.y = 165;
            return TRUE;
        }

        case WM_TIMER:
            if (wParam == ValidationTimerId)
            {
                KillTimer(_dialog, ValidationTimerId);
                StartValidation();
                return TRUE;
            }
            if (wParam == MirrorSyncTimerId && _mirrorLinkManager)
            {
                _mirrorLinkManager->HandleTimer(static_cast<UINT_PTR>(wParam));
                return TRUE;
            }
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_VALIDATE_NOW:
                ScheduleValidation(true);
                return TRUE;

            case IDC_PIPE_COLORS:
                OpenPipeColorDialog();
                return TRUE;

            case IDC_AUTO_VALIDATE:
                if (HIWORD(wParam) == BN_CLICKED &&
                    static_cast<LRESULT>(SendMessageW(_autoValidateCheck, BM_GETCHECK, 0, 0)) == BST_CHECKED)
                {
                    ScheduleValidation(true);
                }
                return TRUE;

            case IDCANCEL:
                Hide();
                return TRUE;
            }
            break;

        case WM_NOTIFY:
        {
            const auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header == nullptr)
            {
                break;
            }

            if (header->hwndFrom == _errorList && header->code == NM_DBLCLK)
            {
                NavigateToSelectedError();
                return TRUE;
            }

            if (header->hwndFrom == _nppData._nppHandle)
            {
                switch (LOWORD(header->code))
                {
                case DMN_CLOSE:
                case DMN_SWITCHOFF:
                    UpdateMenuCheck(false);
                    return TRUE;

                case DMN_SWITCHIN:
                    UpdateMenuCheck(true);
                    ScheduleValidation(true);
                    return TRUE;

                default:
                    break;
                }
            }
            break;
        }

        case WM_CLOSE:
            Hide();
            return TRUE;

        case WorkerResultMessage:
        {
            std::unique_ptr<ValidationWorkerResult> result(reinterpret_cast<ValidationWorkerResult*>(lParam));
            if (result &&
                result->generation == _generation.load() &&
                result->bufferId == GetCurrentBufferId())
            {
                DisplayResult(*result);
            }
            return TRUE;
        }

        case MirrorSyncResultMessage:
        {
            std::unique_ptr<MirrorSyncWorkerResult> result(
                reinterpret_cast<MirrorSyncWorkerResult*>(lParam));
            if (_mirrorLinkManager)
            {
                _mirrorLinkManager->HandleWorkerResult(std::move(result));
            }
            EnsureCompactToolbarsStable();
            return TRUE;
        }

        case WM_DESTROY:
            return TRUE;
        }

        return FALSE;
    }

    void ValidatorWindow::InitializeControls()
    {
        _statusText = GetDlgItem(_dialog, IDC_STATUS_TEXT);
        _errorList = GetDlgItem(_dialog, IDC_ERROR_LIST);
        _validateButton = GetDlgItem(_dialog, IDC_VALIDATE_NOW);
        _colorsButton = GetDlgItem(_dialog, IDC_PIPE_COLORS);
        _autoValidateCheck = GetDlgItem(_dialog, IDC_AUTO_VALIDATE);

        SendMessageW(_autoValidateCheck, BM_SETCHECK, BST_CHECKED, 0);

        ListView_SetExtendedListViewStyle(
            _errorList,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);

        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

        column.pszText = const_cast<wchar_t*>(L"Linha");
        column.cx = 48;
        column.iSubItem = 0;
        ListView_InsertColumn(_errorList, 0, &column);

        column.pszText = const_cast<wchar_t*>(L"ID");
        column.cx = 60;
        column.iSubItem = 1;
        ListView_InsertColumn(_errorList, 1, &column);

        column.pszText = const_cast<wchar_t*>(L"Pipes");
        column.cx = 54;
        column.iSubItem = 2;
        ListView_InsertColumn(_errorList, 2, &column);

        column.pszText = const_cast<wchar_t*>(L"Erro");
        column.cx = 60;
        column.iSubItem = 3;
        ListView_InsertColumn(_errorList, 3, &column);

        if (!_settingsLoaded)
        {
            _pipeColorSettings = LoadPipeColorSettings(_nppData._nppHandle);
            _settingsLoaded = true;
        }

        AllocateEditorVisuals();
        _compactToolbar.SetErrorColor(_pipeColorSettings.brokenTextColor);
        ApplyDarkMode(true);
        _worker = std::make_unique<ValidationWorker>(_dialog);

        RECT client{};
        GetClientRect(_dialog, &client);
        ResizeControls(client.right - client.left, client.bottom - client.top);
    }

    void ValidatorWindow::RegisterDockingPanel()
    {
        if (_dialog == nullptr || _dockingRegistered)
        {
            return;
        }

        _dockData = {};
        _dockData.hClient = _dialog;
        _dockData.pszName = DockTitle;
        _dockData.dlgID = _menuCommandId;
        _dockData.uMask = DWS_DF_CONT_RIGHT;
        _dockData.pszModuleName = PluginModuleName;

        _dockingRegistered = SendMessageW(
            _nppData._nppHandle,
            NPPM_DMMREGASDCKDLG,
            0,
            reinterpret_cast<LPARAM>(&_dockData)) != FALSE;
    }

    void ValidatorWindow::ResizeControls(int clientWidth, int clientHeight)
    {
        if (_errorList == nullptr)
        {
            return;
        }

        constexpr int margin = 5;
        constexpr int gap = 4;
        constexpr int statusHeight = 27;
        constexpr int buttonHeight = 21;
        constexpr int autoWidth = 45;
        constexpr int colorsWidth = 58;
        constexpr int validateWidth = 56;

        const int footerTop = clientHeight - margin - buttonHeight;
        const int listTop = margin + statusHeight;
        const int listHeight = footerTop - gap - listTop;

        MoveWindow(_statusText, margin, margin, clientWidth - margin * 2, statusHeight, TRUE);
        MoveWindow(
            _errorList,
            margin,
            listTop,
            clientWidth - margin * 2,
            listHeight > 1 ? listHeight : 1,
            TRUE);
        MoveWindow(_autoValidateCheck, margin, footerTop, autoWidth, buttonHeight, TRUE);
        MoveWindow(
            _colorsButton,
            clientWidth - margin - validateWidth - gap - colorsWidth,
            footerTop,
            colorsWidth,
            buttonHeight,
            TRUE);
        MoveWindow(
            _validateButton,
            clientWidth - margin - validateWidth,
            footerTop,
            validateWidth,
            buttonHeight,
            TRUE);

        const int errorColumnWidth = clientWidth - margin * 2 - 48 - 60 - 54 - 5;
        if (errorColumnWidth > 45)
        {
            ListView_SetColumnWidth(_errorList, 3, errorColumnWidth);
        }
    }

    void ValidatorWindow::StartValidation()
    {
        if (_worker == nullptr)
        {
            return;
        }

        const HWND scintilla = GetCurrentScintilla();
        if (scintilla == nullptr)
        {
            ClearList();
            SetStatus(L"Nenhum editor ativo.");
            SetCompactToolbarMessage(L"Nenhum editor ativo", false, false);
            return;
        }

        ClearBrokenTextIndicators(scintilla);
        ClearTaggedTextIndicators(scintilla);
        _currentValidationActive = false;
        ClearPipeIndicators(scintilla);

        const LRESULT length = SendMessageW(scintilla, SCI_GETLENGTH, 0, 0);
        if (length < 0 || length > MaxDocumentLength)
        {
            ClearList();
            SetStatus(L"Arquivo acima do limite de 512 MB.");
            SetCompactToolbarMessage(L"Arquivo acima de 512 MB", false, false);
            return;
        }

        const UINT_PTR bufferId = GetCurrentBufferId();
        const std::uint64_t generation = _generation.load();
        _displayedBufferId = bufferId;
        _currentResult = {};

        SetStatus(L"Validando...");
        SetCompactToolbarMessage(L"Validando...", false, false);
        _worker->Submit(
            GetCurrentDocumentText(scintilla),
            GetCurrentFileName(),
            generation,
            bufferId);
    }

    void ValidatorWindow::DisplayResult(ValidationWorkerResult& workerResult)
    {
        _currentResult = std::move(workerResult.validation);
        const PipeValidationResult& result = _currentResult;
        const HWND scintilla = GetCurrentScintilla();
        ClearList();
        _currentValidationActive = result.validationActive;

        if (scintilla != nullptr)
        {
            ClearBrokenTextIndicators(scintilla);
            ClearTaggedTextIndicators(scintilla);
            ClearPipeIndicators(scintilla);
        }

        if (!result.validationActive)
        {
            if (result.headerPresent)
            {
                SetStatus(L"Cabecalho |V.x|QtdPipe| invalido. Ignorado.");
                SetCompactToolbarMessage(L"Cabecalho invalido", false, false);
            }
            else
            {
                SetStatus(L"Sem cabecalho valido. Verificacao ignorada.");
                SetCompactToolbarMessage(L"Sem validacao", false, false);
            }
            return;
        }

        for (const PipeRecordError& error : result.errors)
        {
            AddErrorRow(error);
        }

        if (scintilla != nullptr)
        {
            ApplyBrokenTextIndicators(scintilla, result);
            RefreshVisiblePipeColors(false);
        }

        std::wostringstream status;
        if (result.mode == PipeValidationMode::TranslationFile)
        {
            status << L"Traducao: ";
            for (const char ch : result.translationFileName)
            {
                status << static_cast<wchar_t>(static_cast<unsigned char>(ch));
            }
        }
        else
        {
            status << L"V.";
            for (const char ch : result.version)
            {
                status << static_cast<wchar_t>(static_cast<unsigned char>(ch));
            }
        }
        status << L" | " << result.expectedPipes << L" pipes | ";

        if (result.recordCount == 0)
        {
            status << L"sem registros ID|";
        }
        else if (result.errors.empty())
        {
            status << result.recordCount << L" registro(s) OK";
        }
        else
        {
            status << result.errors.size() << L" erro(s) / " << result.recordCount << L" registro(s)";
        }

        SetStatus(status.str());
        UpdateCompactStatus();
    }

    void ValidatorWindow::ClearList()
    {
        if (_errorList != nullptr)
        {
            ListView_DeleteAllItems(_errorList);
        }
    }

    void ValidatorWindow::AddErrorRow(const PipeRecordError& error)
    {
        const int row = ListView_GetItemCount(_errorList);
        const std::wstring lineText = FormatLineRange(error.startLine, error.endLine);
        const std::wstring idText = std::to_wstring(error.id);
        const std::wstring pipesText =
            ToWideNumber(error.actualPipes) + L"/" + ToWideNumber(error.expectedPipes);
        const std::wstring differenceText = BuildDifferenceText(error);

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.iSubItem = 0;
        item.pszText = const_cast<wchar_t*>(lineText.c_str());
        item.lParam = static_cast<LPARAM>(error.startLine);
        ListView_InsertItem(_errorList, &item);

        ListView_SetItemText(_errorList, row, 1, const_cast<wchar_t*>(idText.c_str()));
        ListView_SetItemText(_errorList, row, 2, const_cast<wchar_t*>(pipesText.c_str()));
        ListView_SetItemText(_errorList, row, 3, const_cast<wchar_t*>(differenceText.c_str()));
    }

    void ValidatorWindow::NavigateToSelectedError()
    {
        const int selected = ListView_GetNextItem(_errorList, -1, LVNI_SELECTED);
        if (selected < 0 || _displayedBufferId != GetCurrentBufferId())
        {
            return;
        }

        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = selected;
        if (!ListView_GetItem(_errorList, &item) || item.lParam <= 0)
        {
            return;
        }

        NavigateToLine(static_cast<std::size_t>(item.lParam));
    }

    void ValidatorWindow::NavigateToLine(std::size_t oneBasedLine)
    {
        if (oneBasedLine == 0 || _displayedBufferId != GetCurrentBufferId())
        {
            return;
        }

        const HWND scintilla = GetCurrentScintilla();
        if (scintilla == nullptr)
        {
            return;
        }

        const WPARAM zeroBasedLine = static_cast<WPARAM>(oneBasedLine - 1U);
        SendMessageW(scintilla, SCI_ENSUREVISIBLE, zeroBasedLine, 0);
        SendMessageW(scintilla, SCI_GOTOLINE, zeroBasedLine, 0);
        SetFocus(scintilla);
    }

    void ValidatorWindow::OpenPipeColorDialog()
    {
        PipeColorSettings updated = _pipeColorSettings;
        if (!PipeColorDialog::Show(
                _instance,
                _dialog,
                _nppData._nppHandle,
                updated))
        {
            return;
        }

        _pipeColorSettings = updated;
        SavePipeColorSettings(_nppData._nppHandle, _pipeColorSettings);
        _compactToolbar.SetErrorColor(_pipeColorSettings.brokenTextColor);
        ConfigureAllEditorVisuals();
        RefreshVisiblePipeColors(true);
        UpdateCompactStatus();
    }

    void ValidatorWindow::CreateCompactToolbar()
    {
        if (_compactToolbar.IsCreated())
        {
            return;
        }

        const bool created = _compactToolbar.Create(
            _instance,
            _nppData._nppHandle,
            [](void* context, std::size_t oneBasedLine)
            {
                if (context != nullptr)
                {
                    static_cast<ValidatorWindow*>(context)->NavigateToLine(oneBasedLine);
                }
            },
            [](void* context)
            {
                if (context != nullptr)
                {
                    static_cast<ValidatorWindow*>(context)->Toggle();
                }
            },
            this);

        if (created)
        {
            _compactToolbar.SetErrorColor(_pipeColorSettings.brokenTextColor);
            UpdateCompactStatus();
        }
    }

    void ValidatorWindow::CreateMirrorToolbar()
    {
        if (!_mirrorToolbar.IsCreated())
        {
            _mirrorToolbar.Create(
                _instance,
                _nppData._nppHandle,
                [](void* context)
                {
                    auto* self = static_cast<ValidatorWindow*>(context);
                    if (self != nullptr && self->_mirrorLinkManager)
                    {
                        self->OpenMirrorLinkDialog();
                    }
                },
                [](void* context)
                {
                    auto* self = static_cast<ValidatorWindow*>(context);
                    if (self != nullptr && self->_mirrorLinkManager)
                    {
                        self->_mirrorLinkManager->ManualSync();
                        self->EnsureCompactToolbarsStable();
                    }
                },
                [](void* context)
                {
                    auto* self = static_cast<ValidatorWindow*>(context);
                    if (self != nullptr)
                    {
                        self->OpenFlagTool();
                    }
                },
                this);
        }

        if (_mirrorToolbar.IsCreated())
        {
            _compactToolbar.SetRightReservedWidthLogical(
                _mirrorToolbar.ReservedWidthLogical());
        }
    }

    void ValidatorWindow::OpenMirrorLinkDialog()
    {
        if (!_mirrorLinkManager)
        {
            return;
        }

        MirrorLinkDialog dialog(_instance, _nppData, *_mirrorLinkManager);
        dialog.Show(_nppData._nppHandle);

        // Reafirma a posicao/z-order dos dois blocos sem recria-los.
        EnsureCompactToolbarsStable();
    }

    void ValidatorWindow::OpenFlagTool()
    {
        if (!_flagToolWindow.Show(_instance, _nppData._nppHandle))
        {
            MessageBoxW(
                _nppData._nppHandle,
                L"Nao foi possivel abrir o FlagTool.",
                L"NppGrandFantasia",
                MB_OK | MB_ICONERROR);
        }

        // Abrir uma janela auxiliar nao deve alterar nem recriar os blocos da toolbar.
        EnsureCompactToolbarsStable();
    }

    void ValidatorWindow::EnsureCompactToolbarsStable()
    {
        // O Notepad++ pode executar TB_AUTOSIZE/relayout depois de trocar um
        // documento internamente para registrar o dirty state ou salvar o C_.
        // Os controles nao sao recriados: reafirmamos WS_VISIBLE, z-order e a
        // geometria anterior imediatamente ao terminar a operacao.
        if (!_compactToolbar.IsCreated())
        {
            CreateCompactToolbar();
        }
        if (!_mirrorToolbar.IsCreated())
        {
            CreateMirrorToolbar();
        }

        if (_mirrorToolbar.IsCreated())
        {
            _compactToolbar.SetRightReservedWidthLogical(
                _mirrorToolbar.ReservedWidthLogical());
            _mirrorToolbar.EnsureLayout();
        }
        if (_compactToolbar.IsCreated())
        {
            _compactToolbar.EnsureLayout();
        }
    }

    void ValidatorWindow::SetCompactToolbarMessage(
        const std::wstring& text,
        bool active,
        bool hasError)
    {
        if (!_compactToolbar.IsCreated())
        {
            CreateCompactToolbar();
        }

        _compactToolbar.SetStatus(text, active, hasError);
    }

    void ValidatorWindow::AllocateEditorVisuals()
    {
        // O indicador de textos $valor$ e alocado primeiro para que as cores
        // especificas dos pipes, IDs e erros tenham prioridade visual.
        if (_taggedTextIndicator < 0)
        {
            int taggedTextIndicator = -1;
            if (SendMessageW(
                    _nppData._nppHandle,
                    NPPM_ALLOCATEINDICATOR,
                    1,
                    reinterpret_cast<LPARAM>(&taggedTextIndicator)) != FALSE)
            {
                _taggedTextIndicator = taggedTextIndicator;
            }
        }

        if (_firstPipeIndicator < 0)
        {
            int firstIndicator = -1;
            if (SendMessageW(
                    _nppData._nppHandle,
                    NPPM_ALLOCATEINDICATOR,
                    4,
                    reinterpret_cast<LPARAM>(&firstIndicator)) != FALSE)
            {
                _firstPipeIndicator = firstIndicator;
            }
        }

        if (_validIdIndicator < 0)
        {
            int validIdIndicator = -1;
            if (SendMessageW(
                    _nppData._nppHandle,
                    NPPM_ALLOCATEINDICATOR,
                    1,
                    reinterpret_cast<LPARAM>(&validIdIndicator)) != FALSE)
            {
                _validIdIndicator = validIdIndicator;
            }
        }

        if (_brokenTextIndicator < 0)
        {
            int brokenTextIndicator = -1;
            if (SendMessageW(
                    _nppData._nppHandle,
                    NPPM_ALLOCATEINDICATOR,
                    1,
                    reinterpret_cast<LPARAM>(&brokenTextIndicator)) != FALSE)
            {
                _brokenTextIndicator = brokenTextIndicator;
            }
        }

        ConfigureAllEditorVisuals();
    }

    void ValidatorWindow::ConfigureEditorVisuals(HWND scintilla)
    {
        if (scintilla == nullptr)
        {
            return;
        }

        if (_taggedTextIndicator >= 0)
        {
            SendMessageW(
                scintilla,
                SCI_INDICSETSTYLE,
                static_cast<WPARAM>(_taggedTextIndicator),
                INDIC_TEXTFORE);
            SendMessageW(
                scintilla,
                SCI_INDICSETFLAGS,
                static_cast<WPARAM>(_taggedTextIndicator),
                SC_INDICFLAG_VALUEFORE);
        }

        if (_firstPipeIndicator >= 0)
        {
            for (int index = 0; index < 4; ++index)
            {
                SendMessageW(
                    scintilla,
                    SCI_INDICSETSTYLE,
                    static_cast<WPARAM>(_firstPipeIndicator + index),
                    INDIC_TEXTFORE);
                SendMessageW(
                    scintilla,
                    SCI_INDICSETFORE,
                    static_cast<WPARAM>(_firstPipeIndicator + index),
                    static_cast<LPARAM>(_pipeColorSettings.colors[static_cast<std::size_t>(index)]));
            }
        }

        if (_validIdIndicator >= 0)
        {
            SendMessageW(
                scintilla,
                SCI_INDICSETSTYLE,
                static_cast<WPARAM>(_validIdIndicator),
                INDIC_TEXTFORE);
            SendMessageW(
                scintilla,
                SCI_INDICSETFORE,
                static_cast<WPARAM>(_validIdIndicator),
                static_cast<LPARAM>(_pipeColorSettings.validIdColor));
        }

        if (_brokenTextIndicator >= 0)
        {
            SendMessageW(
                scintilla,
                SCI_INDICSETSTYLE,
                static_cast<WPARAM>(_brokenTextIndicator),
                INDIC_TEXTFORE);
            SendMessageW(
                scintilla,
                SCI_INDICSETFORE,
                static_cast<WPARAM>(_brokenTextIndicator),
                static_cast<LPARAM>(_pipeColorSettings.brokenTextColor));
        }

        InvalidateRect(scintilla, nullptr, FALSE);
    }

    void ValidatorWindow::ConfigureAllEditorVisuals()
    {
        ConfigureEditorVisuals(_nppData._scintillaMainHandle);
        ConfigureEditorVisuals(_nppData._scintillaSecondHandle);
    }

    void ValidatorWindow::ClearPipeIndicators(HWND scintilla)
    {
        if (scintilla == nullptr ||
            (_firstPipeIndicator < 0 && _validIdIndicator < 0))
        {
            return;
        }

        const LRESULT length = SendMessageW(scintilla, SCI_GETLENGTH, 0, 0);
        if (length <= 0)
        {
            return;
        }

        if (_firstPipeIndicator >= 0)
        {
            for (int index = 0; index < 4; ++index)
            {
                SendMessageW(
                    scintilla,
                    SCI_SETINDICATORCURRENT,
                    static_cast<WPARAM>(_firstPipeIndicator + index),
                    0);
                SendMessageW(
                    scintilla,
                    SCI_INDICATORCLEARRANGE,
                    0,
                    static_cast<LPARAM>(length));
            }
        }

        if (_validIdIndicator >= 0)
        {
            SendMessageW(
                scintilla,
                SCI_SETINDICATORCURRENT,
                static_cast<WPARAM>(_validIdIndicator),
                0);
            SendMessageW(
                scintilla,
                SCI_INDICATORCLEARRANGE,
                0,
                static_cast<LPARAM>(length));
        }
    }

    void ValidatorWindow::ClearTaggedTextIndicators(HWND scintilla)
    {
        if (scintilla == nullptr || _taggedTextIndicator < 0)
        {
            return;
        }

        const LRESULT length = SendMessageW(scintilla, SCI_GETLENGTH, 0, 0);
        if (length <= 0)
        {
            return;
        }

        SendMessageW(
            scintilla,
            SCI_SETINDICATORCURRENT,
            static_cast<WPARAM>(_taggedTextIndicator),
            0);
        SendMessageW(
            scintilla,
            SCI_INDICATORCLEARRANGE,
            0,
            static_cast<LPARAM>(length));
    }

    void ValidatorWindow::ApplyVisibleTaggedTextColors(
        HWND scintilla,
        Sci_Position firstDocumentLine,
        Sci_Position lastDocumentLine)
    {
        if (scintilla == nullptr || _taggedTextIndicator < 0 ||
            firstDocumentLine < 0 || lastDocumentLine < firstDocumentLine)
        {
            return;
        }

        const std::size_t firstVisibleLine =
            static_cast<std::size_t>(firstDocumentLine) + 1U;
        const std::size_t lastVisibleLine =
            static_cast<std::size_t>(lastDocumentLine) + 1U;

        for (const PipeRecordInfo& record : _currentResult.records)
        {
            if (!record.IsValid() ||
                record.endLine < firstVisibleLine ||
                record.startLine > lastVisibleLine)
            {
                continue;
            }

            bool hasActiveColor = false;
            PackedRgb activeColor = 0;
            const std::size_t scanEndLine = std::min(record.endLine, lastVisibleLine);

            // Comeca no inicio do registro para recuperar a ultima cor ativa mesmo
            // quando a primeira linha visivel e uma continuacao sem novo $valor$.
            for (std::size_t oneBasedLine = record.startLine;
                 oneBasedLine <= scanEndLine;
                 ++oneBasedLine)
            {
                const Sci_Position documentLine =
                    static_cast<Sci_Position>(oneBasedLine - 1U);
                std::string line;
                Sci_Position lineStart = -1;
                if (!ReadScintillaLine(scintilla, documentLine, line, lineStart))
                {
                    continue;
                }

                const std::size_t contentLength = GetLineContentLength(line);
                const bool shouldPaint = oneBasedLine >= firstVisibleLine;
                const auto idRange = GetRecordIdRange(
                    line,
                    oneBasedLine == record.startLine);

                std::size_t segmentStart = 0;
                std::size_t position = 0;
                while (position < contentLength)
                {
                    GrandFantasiaColorTag tag{};
                    if (!TryParseGrandFantasiaColorTag(
                            std::string_view(line.data(), contentLength),
                            position,
                            tag))
                    {
                        ++position;
                        continue;
                    }

                    if (hasActiveColor && shouldPaint && position > segmentStart)
                    {
                        FillTaggedTextRange(
                            scintilla,
                            _taggedTextIndicator,
                            lineStart,
                            line,
                            segmentStart,
                            position,
                            activeColor,
                            idRange);
                    }

                    activeColor = tag.color;
                    hasActiveColor = true;
                    segmentStart = position;
                    position += tag.length;
                }

                if (hasActiveColor && shouldPaint && contentLength > segmentStart)
                {
                    FillTaggedTextRange(
                        scintilla,
                        _taggedTextIndicator,
                        lineStart,
                        line,
                        segmentStart,
                        contentLength,
                        activeColor,
                        idRange);
                }
            }
        }
    }

    void ValidatorWindow::ClearBrokenTextIndicators(HWND scintilla)
    {
        if (scintilla == nullptr || _brokenTextIndicator < 0)
        {
            return;
        }

        const LRESULT length = SendMessageW(scintilla, SCI_GETLENGTH, 0, 0);
        if (length <= 0)
        {
            return;
        }

        SendMessageW(
            scintilla,
            SCI_SETINDICATORCURRENT,
            static_cast<WPARAM>(_brokenTextIndicator),
            0);
        SendMessageW(
            scintilla,
            SCI_INDICATORCLEARRANGE,
            0,
            static_cast<LPARAM>(length));
    }

    void ValidatorWindow::ApplyBrokenTextIndicators(
        HWND scintilla,
        const PipeValidationResult& result)
    {
        if (scintilla == nullptr || _brokenTextIndicator < 0)
        {
            return;
        }

        SendMessageW(
            scintilla,
            SCI_SETINDICATORCURRENT,
            static_cast<WPARAM>(_brokenTextIndicator),
            0);

        for (const PipeRecordError& error : result.errors)
        {
            if (error.startLine == 0 || error.endLine < error.startLine)
            {
                continue;
            }

            const Sci_Position startPosition = static_cast<Sci_Position>(SendMessageW(
                scintilla,
                SCI_POSITIONFROMLINE,
                static_cast<WPARAM>(error.startLine - 1U),
                0));
            const Sci_Position endPosition = static_cast<Sci_Position>(SendMessageW(
                scintilla,
                SCI_GETLINEENDPOSITION,
                static_cast<WPARAM>(error.endLine - 1U),
                0));

            if (startPosition < 0 || endPosition <= startPosition)
            {
                continue;
            }

            SendMessageW(
                scintilla,
                SCI_INDICATORFILLRANGE,
                static_cast<WPARAM>(startPosition),
                static_cast<LPARAM>(endPosition - startPosition));
        }
    }

    HWND ValidatorWindow::GetCurrentScintilla() const
    {
        int currentView = 0;
        SendMessageW(
            _nppData._nppHandle,
            NPPM_GETCURRENTSCINTILLA,
            0,
            reinterpret_cast<LPARAM>(&currentView));

        return currentView == 1 ? _nppData._scintillaSecondHandle : _nppData._scintillaMainHandle;
    }

    UINT_PTR ValidatorWindow::GetCurrentBufferId() const
    {
        return static_cast<UINT_PTR>(SendMessageW(_nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
    }

    std::string ValidatorWindow::GetCurrentDocumentText(HWND scintilla) const
    {
        const LRESULT length = SendMessageW(scintilla, SCI_GETLENGTH, 0, 0);
        if (length <= 0)
        {
            return {};
        }

        std::string text(static_cast<std::size_t>(length) + 1U, '\0');
        SendMessageW(
            scintilla,
            SCI_GETTEXT,
            static_cast<WPARAM>(text.size()),
            reinterpret_cast<LPARAM>(text.data()));
        text.resize(static_cast<std::size_t>(length));
        return text;
    }

    std::string ValidatorWindow::GetCurrentFileName() const
    {
        // NPPM_GETFILENAME recebe o tamanho do buffer em wchar_t.
        // 32768 cobre o limite de caminhos Unicode do Windows e tambem nomes
        // de buffers ainda nao salvos, como "new 1".
        std::vector<wchar_t> fileName(32768U, L'\0');
        if (SendMessageW(
                _nppData._nppHandle,
                NPPM_GETFILENAME,
                static_cast<WPARAM>(fileName.size()),
                reinterpret_cast<LPARAM>(fileName.data())) == FALSE)
        {
            return {};
        }

        const int requiredBytes = WideCharToMultiByte(
            CP_UTF8,
            0,
            fileName.data(),
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (requiredBytes <= 1)
        {
            return {};
        }

        std::string utf8(static_cast<std::size_t>(requiredBytes), '\0');
        if (WideCharToMultiByte(
                CP_UTF8,
                0,
                fileName.data(),
                -1,
                utf8.data(),
                requiredBytes,
                nullptr,
                nullptr) <= 0)
        {
            return {};
        }

        utf8.pop_back(); // remove o terminador nulo convertido
        return utf8;
    }

    void ValidatorWindow::SetStatus(const std::wstring& text)
    {
        if (_statusText != nullptr)
        {
            SetWindowTextW(_statusText, text.c_str());
        }
    }

    void ValidatorWindow::UpdateMenuCheck(bool checked) const
    {
        if (_menuCommandId != 0)
        {
            SendMessageW(
                _nppData._nppHandle,
                NPPM_SETMENUITEMCHECK,
                static_cast<WPARAM>(_menuCommandId),
                static_cast<LPARAM>(checked ? TRUE : FALSE));
        }
    }
}
