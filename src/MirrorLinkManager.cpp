#include "MirrorLinkManager.h"

#include "npp/Notepad_plus_msgs.h"
#include "npp/ScintillaMinimal.h"

#include <algorithm>
#include <commctrl.h>
#include <array>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace NppGrandFantasia
{
    namespace
    {
        constexpr UINT DebounceMilliseconds = 180;
        constexpr UINT_PTR NotepadSubclassId = 0x4E4749U;
        constexpr LRESULT MaxMirrorDocumentLength = 512LL * 1024LL * 1024LL;
        constexpr int PositionIndexMask = 0x3FFFFFFF;
        constexpr unsigned PositionViewShift = 30U;

        // Valores retornados por NPPM_GETBUFFERENCODING (UniMode do Notepad++).
        constexpr int Uni8Bit = 0;
        constexpr int UniUtf8 = 1;
        constexpr int UniUtf8NoBom = 4;
        constexpr int Uni7Bit = 5;

        constexpr int IdmBase = 40000;
        constexpr int IdmFormat = IdmBase + 5000;
        constexpr int IdmFormatEncode = IdmFormat + 20;
        constexpr int IdmFormatAnsi = IdmFormat + 4;
        constexpr int IdmFormatUtf16Le = IdmFormat + 13;
        constexpr int IdmFormatEncodeLast = IdmFormatEncode + 48;

        constexpr std::array<int, 49> CustomEncodingCodePages = {
            1250, 1251, 1252, 1253, 1254, 1255, 1256, 1257, 1258,
            28591, 28592, 28593, 28594, 28595, 28596, 28597, 28598, 28599,
            -1, -1, 28603, 28604, 28605, -1,
            437, 720, 737, 775, 850, 852, 855, 857, 858, 860, 861, 862,
            863, 865, 866, 869, 950, 936, 932, 949, 51949, 874, 10007,
            21866, 20866,
        };

        bool IsUnicodeMode(int unicodeMode)
        {
            return unicodeMode >= 1 && unicodeMode <= 7;
        }

        UINT ResolveLegacyCodePage(const MirrorDocumentEncoding& encoding)
        {
            if (encoding.saveCodePage > 0)
            {
                return static_cast<UINT>(encoding.saveCodePage);
            }
            if (encoding.internalCodePage > 1)
            {
                return encoding.internalCodePage;
            }
            return GetACP();
        }

        UINT ResolveInternalCodePage(const MirrorDocumentEncoding& encoding)
        {
            // Notepad++ stores every Unicode flavour (UTF-8/UTF-16, with or
            // without BOM) as UTF-8 inside Scintilla. Only uni8Bit keeps a
            // legacy/DBCS byte representation in the document.
            return encoding.unicodeMode == Uni8Bit
                ? ResolveLegacyCodePage(encoding)
                : CP_UTF8;
        }

        bool CanStoreUtf8BytesDirectly(const MirrorDocumentEncoding& encoding)
        {
            if (ResolveInternalCodePage(encoding) != CP_UTF8)
            {
                return false;
            }

            // Estes modos sao gravados pelo Notepad++ sem transcodificar os
            // bytes internos do Scintilla. Isso inclui ANSI quando a code page
            // efetiva do processo e 65001, alem de UTF-8 com/sem BOM.
            return encoding.unicodeMode == Uni8Bit ||
                encoding.unicodeMode == UniUtf8 ||
                encoding.unicodeMode == UniUtf8NoBom;
        }

        std::wstring DecodeUtf8Strict(
            const std::string& utf8,
            std::wstring& error)
        {
            if (utf8.empty())
            {
                return {};
            }
            if (utf8.find('\0') != std::string::npos)
            {
                error = L"O arquivo contem bytes NUL e nao pode ser espelhado com seguranca.";
                return {};
            }
            if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                error = L"O arquivo e grande demais para uma conversao de encoding segura.";
                return {};
            }

            const int required = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                utf8.data(),
                static_cast<int>(utf8.size()),
                nullptr,
                0);
            if (required <= 0)
            {
                error =
                    L"O Scintilla nao conseguiu fornecer o conteudo logico do arquivo como UTF-8 valido. "
                    L"Nenhum arquivo foi alterado.";
                return {};
            }

            std::wstring wide(static_cast<std::size_t>(required), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    utf8.data(),
                    static_cast<int>(utf8.size()),
                    wide.data(),
                    required) <= 0)
            {
                error = L"Falha ao converter o conteudo UTF-8 do arquivo principal para Unicode.";
                return {};
            }
            return wide;
        }

        bool EncodeCodePageStrict(
            const std::wstring& wide,
            UINT requestedCodePage,
            std::string& output,
            std::wstring& error,
            const wchar_t* purpose)
        {
            const UINT codePage = requestedCodePage > 1 ? requestedCodePage : GetACP();
            if (wide.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                error = L"O texto e grande demais para uma conversao de encoding segura.";
                return false;
            }

            BOOL usedDefault = FALSE;
            DWORD flags = codePage == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
            BOOL* usedDefaultPointer = codePage == CP_UTF8 ? nullptr : &usedDefault;

            auto convert = [&](char* destination, int destinationSize)
            {
                SetLastError(ERROR_SUCCESS);
                return WideCharToMultiByte(
                    codePage,
                    flags,
                    wide.data(),
                    static_cast<int>(wide.size()),
                    destination,
                    destinationSize,
                    nullptr,
                    usedDefaultPointer);
            };

            int required = convert(nullptr, 0);
            if (required == 0 && !wide.empty() && codePage != CP_UTF8 &&
                GetLastError() == ERROR_INVALID_FLAGS)
            {
                // Algumas code pages antigas nao aceitam WC_NO_BEST_FIT_CHARS.
                // O round-trip estrito abaixo continua bloqueando conversoes destrutivas.
                flags = 0;
                usedDefault = FALSE;
                required = convert(nullptr, 0);
            }
            if (required == 0 && !wide.empty())
            {
                error = L"Falha ao converter o texto para " + std::wstring(purpose) +
                    L" (code page " + std::to_wstring(codePage) + L").";
                return false;
            }

            output.assign(static_cast<std::size_t>(required), '\0');
            usedDefault = FALSE;
            if (required > 0 && convert(output.data(), required) <= 0)
            {
                output.clear();
                error = L"Falha ao gerar os bytes para " + std::wstring(purpose) + L".";
                return false;
            }
            if (usedDefault != FALSE)
            {
                output.clear();
                error = L"O texto do S_ possui caracteres que nao existem no encoding atual do C_ "
                    L"(code page " + std::to_wstring(codePage) +
                    L"). O C_ foi mantido intacto.";
                return false;
            }

            if (codePage != CP_UTF8 && !wide.empty())
            {
                const int roundTripSize = MultiByteToWideChar(
                    codePage,
                    0,
                    output.data(),
                    static_cast<int>(output.size()),
                    nullptr,
                    0);
                if (roundTripSize <= 0)
                {
                    output.clear();
                    error = L"A validacao do encoding do C_ falhou. O arquivo anterior foi mantido intacto.";
                    return false;
                }
                std::wstring roundTrip(static_cast<std::size_t>(roundTripSize), L'\0');
                if (MultiByteToWideChar(
                        codePage,
                        0,
                        output.data(),
                        static_cast<int>(output.size()),
                        roundTrip.data(),
                        roundTripSize) <= 0 ||
                    roundTrip != wide)
                {
                    output.clear();
                    error = L"A conversao para o encoding do C_ seria destrutiva. O arquivo anterior foi mantido intacto.";
                    return false;
                }
            }
            return true;
        }

        bool ValidateMirrorSaveEncoding(
            const std::wstring& wide,
            const MirrorDocumentEncoding& encoding,
            std::wstring& error)
        {
            if (encoding.unicodeMode < 0)
            {
                error = L"O encoding atual do C_ nao foi reconhecido. O arquivo foi mantido intacto.";
                return false;
            }

            if (encoding.unicodeMode == Uni7Bit)
            {
                for (wchar_t character : wide)
                {
                    if (static_cast<unsigned int>(character) > 0x7FU)
                    {
                        error = L"O texto do S_ possui caracteres que nao podem ser salvos no modo ASCII/7-bit atual do C_. O C_ foi mantido intacto.";
                        return false;
                    }
                }
                return true;
            }

            if (encoding.unicodeMode == 0)
            {
                std::string discarded;
                return EncodeCodePageStrict(
                    wide,
                    ResolveLegacyCodePage(encoding),
                    discarded,
                    error,
                    L"o encoding de salvamento do C_");
            }

            if (!IsUnicodeMode(encoding.unicodeMode))
            {
                error = L"O encoding atual do C_ nao foi reconhecido. O arquivo foi mantido intacto.";
                return false;
            }
            return true;
        }

        bool EncodeForMirrorDocument(
            const std::wstring& wide,
            const std::string& sourceUtf8,
            const MirrorDocumentEncoding& encoding,
            std::string& output,
            std::wstring& error)
        {
            if (!ValidateMirrorSaveEncoding(wide, encoding, error))
            {
                return false;
            }

            const UINT internalCodePage = ResolveInternalCodePage(encoding);
            if (internalCodePage == CP_UTF8)
            {
                output = sourceUtf8;
                return true;
            }

            return EncodeCodePageStrict(
                wide,
                internalCodePage,
                output,
                error,
                L"o documento interno do C_");
        }

        std::wstring CurrentTimestamp()
        {
            SYSTEMTIME time{};
            GetLocalTime(&time);
            wchar_t buffer[64]{};
            swprintf_s(
                buffer,
                L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
                time.wYear,
                time.wMonth,
                time.wDay,
                time.wHour,
                time.wMinute,
                time.wSecond,
                time.wMilliseconds);
            return buffer;
        }
    }

    MirrorSyncWorker::MirrorSyncWorker(HWND resultWindow)
        : _resultWindow(resultWindow),
          _thread(&MirrorSyncWorker::ThreadMain, this)
    {
    }

    MirrorSyncWorker::~MirrorSyncWorker()
    {
        Stop();
    }

    void MirrorSyncWorker::Submit(Job job)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_stopping)
            {
                return;
            }

            // Mantem apenas a versao mais recente de cada par, sem descartar
            // trabalhos pendentes pertencentes a outros vinculos.
            _pendingJobs.erase(
                std::remove_if(
                    _pendingJobs.begin(),
                    _pendingJobs.end(),
                    [&job](const Job& pending)
                    {
                        return pending.sourceBufferId == job.sourceBufferId &&
                            pending.mirrorBufferId == job.mirrorBufferId;
                    }),
                _pendingJobs.end());
            _pendingJobs.push_back(std::move(job));
        }
        _condition.notify_one();
    }

    void MirrorSyncWorker::Stop()
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_stopping)
            {
                return;
            }
            _stopping = true;
            _pendingJobs.clear();
        }
        _condition.notify_one();
        if (_thread.joinable())
        {
            _thread.join();
        }
    }

    MirrorSyncWorkerResult MirrorSyncWorker::Convert(const Job& job)
    {
        const auto started = std::chrono::steady_clock::now();
        MirrorSyncWorkerResult result{};
        result.generation = job.generation;
        result.sourceBufferId = job.sourceBufferId;
        result.mirrorBufferId = job.mirrorBufferId;

        result.different = job.sourceUtf8 != job.mirrorUtf8;
        if (!result.different)
        {
            result.success = true;
            result.conversionMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            return result;
        }

        if (CanStoreUtf8BytesDirectly(job.mirrorEncoding))
        {
            result.targetBytes = job.sourceUtf8;
            result.sourceCharacters = job.sourceUtf8.size();
            result.success = true;
            result.conversionMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            return result;
        }

        std::wstring conversionError;
        const std::wstring logicalText = DecodeUtf8Strict(job.sourceUtf8, conversionError);
        if (!conversionError.empty())
        {
            result.error = std::move(conversionError);
            return result;
        }

        result.sourceCharacters = logicalText.size();
        if (!EncodeForMirrorDocument(
                logicalText,
                job.sourceUtf8,
                job.mirrorEncoding,
                result.targetBytes,
                conversionError))
        {
            result.error = std::move(conversionError);
            return result;
        }

        result.success = true;
        result.conversionMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        return result;
    }

    void MirrorSyncWorker::ThreadMain()
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _condition.wait(lock, [this]() { return _stopping || !_pendingJobs.empty(); });
                if (_stopping)
                {
                    return;
                }
                job = std::move(_pendingJobs.front());
                _pendingJobs.pop_front();
            }

            auto* result = new MirrorSyncWorkerResult(Convert(job));
            if (!PostMessageW(
                    _resultWindow,
                    MirrorSyncResultMessage,
                    0,
                    reinterpret_cast<LPARAM>(result)))
            {
                delete result;
            }
        }
    }

    MirrorLinkManager::MirrorLinkManager(NppData nppData, HWND ownerWindow, MirrorToolbar* toolbar)
        : _nppData(nppData),
          _ownerWindow(ownerWindow),
          _toolbar(toolbar)
    {
    }

    MirrorLinkManager::~MirrorLinkManager()
    {
        Shutdown();
    }

    void MirrorLinkManager::Initialize()
    {
        if (_initialized)
        {
            return;
        }

        if (!CreateHiddenScintillas())
        {
            UpdateToolbar(MirrorToolbarState::Error, L"Nao foi possivel criar os acessores Scintilla internos.");
            return;
        }

        _notepadSubclassInstalled = SetWindowSubclass(
            _nppData._nppHandle,
            NotepadSubclassProc,
            NotepadSubclassId,
            reinterpret_cast<DWORD_PTR>(this)) != FALSE;
        if (!_notepadSubclassInstalled)
        {
            DestroyHiddenScintillas();
            UpdateToolbar(
                MirrorToolbarState::Error,
                L"Nao foi possivel monitorar alteracoes de encoding com seguranca.");
            Log(L"Falha ao instalar o monitor de comandos de encoding.");
            return;
        }

        _worker = std::make_unique<MirrorSyncWorker>(_ownerWindow);
        _initialized = true;
        UpdateToolbarSummary(MirrorToolbarState::Unlinked);
        Log(L"Sistema de espelhamento multiplo inicializado.");
    }

    void MirrorLinkManager::Shutdown()
    {
        KillTimer(_ownerWindow, MirrorSyncTimerId);
        while (!_links.empty())
        {
            UnlinkAt(_links.size() - 1U, L"Encerramento do Notepad++.", false, false);
        }

        if (_worker)
        {
            _worker->Stop();
            _worker.reset();
        }
        if (_notepadSubclassInstalled &&
            _nppData._nppHandle != nullptr &&
            IsWindow(_nppData._nppHandle) != FALSE)
        {
            RemoveWindowSubclass(_nppData._nppHandle, NotepadSubclassProc, NotepadSubclassId);
        }
        _notepadSubclassInstalled = false;
        DestroyHiddenScintillas();
        _dirtySources.clear();
        _deferredMirrorSaves.clear();
        _internalSaveBuffers.clear();
        _initialized = false;
    }

    void MirrorLinkManager::ToggleLinkFromCurrent()
    {
        Initialize();
        OpenDocument source;
        OpenDocument mirror;
        std::wstring error;
        if (!ResolvePairFromCurrent(source, mirror, error))
        {
            UpdateToolbar(MirrorToolbarState::Error, error);
            ShowErrorOnce(error);
            return;
        }

        if (IsPairLinked(source.bufferId, mirror.bufferId))
        {
            UnlinkPair(source.bufferId, mirror.bufferId);
            return;
        }
        if (!LinkPair(source.bufferId, mirror.bufferId, error))
        {
            UpdateToolbar(MirrorToolbarState::Error, error);
            ShowErrorOnce(error);
        }
    }

    std::vector<MirrorPairCandidate> MirrorLinkManager::GetAvailablePairs() const
    {
        const auto documents = EnumerateOpenDocuments();
        std::vector<OpenDocument> sources;
        std::vector<OpenDocument> mirrors;
        for (const OpenDocument& document : documents)
        {
            if (!document.viewVisible)
            {
                continue;
            }
            const std::wstring name = BaseName(document.path);
            if (IsServerName(name))
            {
                sources.push_back(document);
            }
            else if (IsClientName(name))
            {
                mirrors.push_back(document);
            }
        }

        std::vector<MirrorPairCandidate> pairs;
        for (const OpenDocument& source : sources)
        {
            for (const OpenDocument& mirror : mirrors)
            {
                const std::wstring sourceName = BaseName(source.path);
                const std::wstring mirrorName = BaseName(mirror.path);
                if (!NamesMatch(sourceName, mirrorName) || source.bufferId == mirror.bufferId)
                {
                    continue;
                }

                MirrorPairCandidate pair{};
                pair.sourceBufferId = source.bufferId;
                pair.mirrorBufferId = mirror.bufferId;
                pair.sourcePath = source.path;
                pair.mirrorPath = mirror.path;
                pair.sourceName = sourceName;
                pair.mirrorName = mirrorName;
                pair.sourceDirectory = DirectoryName(source.path);
                pair.mirrorDirectory = DirectoryName(mirror.path);
                pair.linked = IsPairLinked(source.bufferId, mirror.bufferId);
                pair.conflict = !pair.linked &&
                    (BufferParticipatesInLink(source.bufferId) || BufferParticipatesInLink(mirror.bufferId));
                pairs.push_back(std::move(pair));
            }
        }

        std::sort(
            pairs.begin(),
            pairs.end(),
            [](const MirrorPairCandidate& left, const MirrorPairCandidate& right)
            {
                const bool leftSameDirectory =
                    _wcsicmp(left.sourceDirectory.c_str(), left.mirrorDirectory.c_str()) == 0;
                const bool rightSameDirectory =
                    _wcsicmp(right.sourceDirectory.c_str(), right.mirrorDirectory.c_str()) == 0;
                if (leftSameDirectory != rightSameDirectory)
                {
                    return leftSameDirectory;
                }
                const int sourceNameCompare = _wcsicmp(left.sourceName.c_str(), right.sourceName.c_str());
                if (sourceNameCompare != 0)
                {
                    return sourceNameCompare < 0;
                }
                const int sourcePathCompare = _wcsicmp(left.sourcePath.c_str(), right.sourcePath.c_str());
                if (sourcePathCompare != 0)
                {
                    return sourcePathCompare < 0;
                }
                return _wcsicmp(left.mirrorPath.c_str(), right.mirrorPath.c_str()) < 0;
            });
        return pairs;
    }

    bool MirrorLinkManager::LinkPair(
        UINT_PTR sourceBufferId,
        UINT_PTR mirrorBufferId,
        std::wstring& error)
    {
        Initialize();
        error.clear();
        if (!_initialized)
        {
            error = L"Nao foi possivel inicializar o sistema de vinculo.";
            return false;
        }
        if (IsPairLinked(sourceBufferId, mirrorBufferId))
        {
            return true;
        }
        if (BufferParticipatesInLink(sourceBufferId) || BufferParticipatesInLink(mirrorBufferId))
        {
            error = L"Um dos arquivos selecionados ja participa de outro vinculo.";
            return false;
        }

        const auto source = FindDocument(sourceBufferId);
        const auto mirror = FindDocument(mirrorBufferId);
        if (!source.has_value() || !mirror.has_value())
        {
            error = L"Um dos arquivos selecionados nao esta mais aberto. Atualize a lista.";
            return false;
        }
        if (!source->viewVisible || !mirror->viewVisible)
        {
            error = L"Os dois arquivos precisam estar em uma view visivel para criar o vinculo.";
            return false;
        }
        if (sourceBufferId == mirrorBufferId)
        {
            error = L"Nao e permitido vincular um arquivo a ele mesmo.";
            return false;
        }

        const std::wstring sourceName = BaseName(source->path);
        const std::wstring mirrorName = BaseName(mirror->path);
        if (!IsServerName(sourceName))
        {
            error = L"O arquivo principal precisa comecar com S_.";
            return false;
        }
        if (!IsClientName(mirrorName))
        {
            error = L"O arquivo espelho precisa comecar com C_.";
            return false;
        }
        if (!NamesMatch(sourceName, mirrorName))
        {
            error = L"Os nomes nao correspondem depois dos prefixos S_ e C_.";
            return false;
        }
        return CreateLink(*source, *mirror, error);
    }

    bool MirrorLinkManager::UnlinkPair(UINT_PTR sourceBufferId, UINT_PTR mirrorBufferId)
    {
        for (std::size_t index = 0; index < _links.size(); ++index)
        {
            const LinkState& link = *_links[index];
            if (link.sourceBufferId == sourceBufferId && link.mirrorBufferId == mirrorBufferId)
            {
                UnlinkAt(index, L"Vinculo removido pelo usuario.", false, false);
                return true;
            }
        }
        return false;
    }

    void MirrorLinkManager::UnlinkByUser()
    {
        while (!_links.empty())
        {
            UnlinkAt(_links.size() - 1U, L"Vinculo removido pelo usuario.", false, false);
        }
    }

    bool MirrorLinkManager::CreateLink(
        const OpenDocument& source,
        const OpenDocument& mirror,
        std::wstring& error)
    {
        _errorMessageShown = false;
        UpdateToolbar(MirrorToolbarState::Syncing, L"Validando e sincronizando o novo par...");

        auto link = std::make_unique<LinkState>();
        link->sourceBufferId = source.bufferId;
        link->mirrorBufferId = mirror.bufferId;
        link->sourcePath = source.path;
        link->mirrorPath = mirror.path;
        link->sourceNormalizedPath = source.normalizedPath;
        link->mirrorNormalizedPath = mirror.normalizedPath;
        link->generation = ++_nextGeneration;

        if (!CaptureDocumentPointers(*link, source, mirror, error))
        {
            ReleaseLinkDocuments(*link);
            UpdateToolbar(MirrorToolbarState::Error, error);
            Log(L"Vinculo recusado: " + error);
            return false;
        }
        if (link->sourceEncoding.unicodeMode < 0 || link->mirrorEncoding.unicodeMode < 0)
        {
            error = L"Nao foi possivel detectar o encoding dos dois arquivos.";
            ReleaseLinkDocuments(*link);
            UpdateToolbar(MirrorToolbarState::Error, error);
            Log(L"Vinculo recusado: " + error);
            return false;
        }

        Log(L"Arquivo principal: " + link->sourcePath);
        Log(L"Arquivo espelho: " + link->mirrorPath);
        Log(L"Encoding S_: " + EncodingName(link->sourceEncoding));
        Log(L"Encoding C_: " + EncodingName(link->mirrorEncoding));

        std::wstring syncError;
        if (!SynchronizeImmediate(*link, L"criacao do vinculo", false, &syncError))
        {
            error = syncError.empty() ? L"Falha na sincronizacao inicial." : syncError;
            SetMirrorReadOnly(*link, link->mirrorWasReadOnly);
            ReleaseLinkDocuments(*link);
            UpdateToolbar(MirrorToolbarState::Error, error);
            Log(L"Vinculo recusado: " + error);
            return false;
        }

        SetMirrorReadOnly(*link, true);
        Log(L"Vinculo criado: " + link->sourcePath + L" -> " + link->mirrorPath);
        _links.push_back(std::move(link));
        UpdateToolbarSummary();
        return true;
    }

    void MirrorLinkManager::ManualSync()
    {
        if (_links.empty())
        {
            UpdateToolbarSummary(MirrorToolbarState::Unlinked);
            return;
        }

        UpdateToolbar(MirrorToolbarState::Syncing, L"Sincronizando e salvando os vinculos ativos...");
        std::vector<std::wstring> failures;
        std::size_t completed = 0;
        for (const auto& item : _links)
        {
            LinkState& link = *item;
            std::wstring error;
            if (!ValidateLink(link, error))
            {
                failures.push_back(BaseName(link.sourcePath) + L": " + error);
                continue;
            }
            if (!SynchronizeImmediate(link, L"botao Sync", false, &error) || !SavePair(link, error))
            {
                failures.push_back(BaseName(link.sourcePath) + L": " + error);
                continue;
            }
            ++completed;
        }

        if (!failures.empty())
        {
            std::wstring detail = L"Falha em " + std::to_wstring(failures.size()) + L" vinculo(s).";
            for (const std::wstring& failure : failures)
            {
                detail += L"\r\n" + failure;
            }
            UpdateToolbar(MirrorToolbarState::Error, detail);
            ShowErrorOnce(detail);
            Log(detail);
            return;
        }

        _errorMessageShown = false;
        UpdateToolbarSummary(
            MirrorToolbarState::Linked,
            std::to_wstring(completed) + L" vinculo(s) sincronizado(s) e salvo(s).");
        Log(L"Sincronizacao manual concluida para " + std::to_wstring(completed) + L" vinculo(s).");
    }

    void MirrorLinkManager::ScheduleFromModification(UINT_PTR modifiedBufferId)
    {
        if (_isApplyingMirrorUpdate || _isCapturingDocuments)
        {
            return;
        }

        LinkState* link = FindLinkBySource(modifiedBufferId);
        if (link == nullptr)
        {
            link = FindLinkByMirror(modifiedBufferId);
        }
        if (link == nullptr)
        {
            return;
        }

        std::wstring error;
        if (!ValidateLink(*link, error, false))
        {
            UnlinkForBuffer(modifiedBufferId, error, true, false);
            return;
        }
        if (modifiedBufferId == link->mirrorBufferId)
        {
            Log(L"Alteracao externa detectada no C_. O conteudo sera restaurado: " + link->mirrorPath);
        }

        link->generation = ++_nextGeneration;
        _dirtySources.insert(link->sourceBufferId);
        KillTimer(_ownerWindow, MirrorSyncTimerId);
        SetTimer(_ownerWindow, MirrorSyncTimerId, DebounceMilliseconds, nullptr);
    }

    void MirrorLinkManager::HandleTimer(UINT_PTR timerId)
    {
        if (timerId != MirrorSyncTimerId)
        {
            return;
        }
        KillTimer(_ownerWindow, MirrorSyncTimerId);

        const std::vector<UINT_PTR> pending(_dirtySources.begin(), _dirtySources.end());
        _dirtySources.clear();
        for (UINT_PTR sourceBufferId : pending)
        {
            if (LinkState* link = FindLinkBySource(sourceBufferId))
            {
                SubmitBackgroundSync(*link);
            }
        }
    }

    void MirrorLinkManager::HandleWorkerResult(std::unique_ptr<MirrorSyncWorkerResult> result)
    {
        if (!result)
        {
            return;
        }
        LinkState* link = FindLinkByPair(result->sourceBufferId, result->mirrorBufferId);
        if (link == nullptr || result->generation != link->generation)
        {
            return;
        }
        if (!result->success)
        {
            link->lastError = result->error;
            UpdateToolbar(MirrorToolbarState::Error, result->error);
            Log(L"Falha de conversao de encoding em " + link->sourcePath + L": " + result->error);
            return;
        }
        if (!result->different)
        {
            link->lastError.clear();
            UpdateToolbarSummary();
            return;
        }

        std::wstring error;
        if (!ApplyConvertedMirrorText(
                *link,
                result->targetBytes,
                result->sourceCharacters,
                result->conversionMilliseconds,
                &error))
        {
            link->lastError = error;
            UpdateToolbar(MirrorToolbarState::Error, error);
            return;
        }
        link->lastError.clear();
        UpdateToolbarSummary();
    }

    void MirrorLinkManager::HandleBeforeSave(UINT_PTR bufferId)
    {
        if (_isApplyingMirrorUpdate || _internalSaveBuffers.count(bufferId) != 0U)
        {
            return;
        }
        LinkState* link = FindLinkBySource(bufferId);
        if (link == nullptr)
        {
            return;
        }

        Log(L"Salvamento do S_ iniciado: " + link->sourcePath);
        std::wstring error;
        link->sourceSaveSyncFailed = !SynchronizeImmediate(
            *link,
            L"salvamento do S_",
            false,
            &error,
            false);
        if (link->sourceSaveSyncFailed)
        {
            link->lastError = error;
            UpdateToolbar(MirrorToolbarState::Error, error);
            return;
        }

        // NPPN_FILEBEFORESAVE e reentrante dentro do fluxo de salvamento do
        // documento atual. Caso a ultima edicao ainda estivesse no debounce,
        // registra o dirty state do C_ logo depois que o Notepad++ voltar ao
        // message loop, sem ativar outra aba dentro desta notificacao.
        std::wstring attachError;
        if (!link->mirrorDirtyRegisteredWithNotepad &&
            AttachAccessors(*link, attachError) &&
            SendMessageW(_mirrorAccessor, SCI_GETMODIFY, 0, 0) != FALSE)
        {
            if (!PostMessageW(
                    _ownerWindow,
                    MirrorDeferredDirtyMessage,
                    static_cast<WPARAM>(link->mirrorBufferId),
                    0))
            {
                const std::wstring postError =
                    L"Nao foi possivel agendar o estado nao salvo do C_: " + link->mirrorPath;
                link->lastError = postError;
                UpdateToolbar(MirrorToolbarState::Error, postError);
                Log(postError);
            }
        }
    }

    void MirrorLinkManager::HandleFileSaved(UINT_PTR bufferId)
    {
        if (_internalSaveBuffers.count(bufferId) != 0U)
        {
            return;
        }

        if (LinkState* sourceLink = FindLinkBySource(bufferId))
        {
            std::wstring attachError;
            if (!AttachAccessors(*sourceLink, attachError))
            {
                sourceLink->lastError = attachError;
                UpdateToolbar(MirrorToolbarState::Error, attachError);
                return;
            }
            const bool sourceStillModified =
                SendMessageW(_sourceAccessor, SCI_GETMODIFY, 0, 0) != FALSE;
            if (sourceStillModified)
            {
                const std::wstring error = L"O Notepad++ nao concluiu o salvamento de " + sourceLink->sourcePath + L".";
                sourceLink->lastError = error;
                UpdateToolbar(MirrorToolbarState::Error, error);
                Log(error);
                return;
            }
            if (sourceLink->sourceSaveSyncFailed)
            {
                sourceLink->sourceSaveSyncFailed = false;
                const std::wstring error =
                    L"O S_ foi salvo, mas o C_ nao foi salvo porque a sincronizacao segura falhou.";
                sourceLink->lastError = error;
                UpdateToolbar(MirrorToolbarState::Error, error);
                Log(error);
                return;
            }

            Log(L"S_ salvo pelo Notepad++: " + sourceLink->sourcePath);
            QueueMirrorSave(*sourceLink);
            return;
        }

        if (LinkState* mirrorLink = FindLinkByMirror(bufferId))
        {
            std::wstring attachError;
            if (AttachAccessors(*mirrorLink, attachError))
            {
                const bool modified = SendMessageW(_mirrorAccessor, SCI_GETMODIFY, 0, 0) != FALSE;
                mirrorLink->mirrorDirtyRegisteredWithNotepad = modified;
                Log(modified
                    ? L"O C_ permaneceu modificado apos a notificacao de save: " + mirrorLink->mirrorPath
                    : L"C_ salvo pelo Notepad++: " + mirrorLink->mirrorPath);
            }
        }
    }

    void MirrorLinkManager::HandleDeferredMirrorDirty(UINT_PTR mirrorBufferId)
    {
        LinkState* link = FindLinkByMirror(mirrorBufferId);
        if (link == nullptr || link->mirrorDirtyRegisteredWithNotepad)
        {
            return;
        }

        std::wstring error;
        if (!ValidateLink(*link, error, false) || !AttachAccessors(*link, error))
        {
            link->lastError = error;
            UpdateToolbar(MirrorToolbarState::Error, error);
            Log(error);
            return;
        }
        if (SendMessageW(_mirrorAccessor, SCI_GETMODIFY, 0, 0) == FALSE)
        {
            return;
        }
        if (!MarkMirrorDirtyThroughNotepad(*link, error))
        {
            link->lastError = error;
            UpdateToolbar(MirrorToolbarState::Error, error);
            ShowErrorOnce(error);
            Log(error);
            return;
        }

        link->mirrorDirtyRegisteredWithNotepad = true;
        Log(L"Dirty state diferido do C_ concluido: " + link->mirrorPath);
    }

    void MirrorLinkManager::QueueMirrorSave(LinkState& link)
    {
        if (link.deferredSaveQueued)
        {
            return;
        }
        link.deferredSaveQueued = true;
        _deferredMirrorSaves.push_back(link.mirrorBufferId);
        if (!PostMessageW(_ownerWindow, MirrorDeferredSaveMessage, 0, 0))
        {
            link.deferredSaveQueued = false;
            _deferredMirrorSaves.pop_back();
            const std::wstring error = L"Nao foi possivel agendar o salvamento automatico de " + link.mirrorPath + L".";
            link.lastError = error;
            UpdateToolbar(MirrorToolbarState::Error, error);
            Log(error);
        }
    }

    void MirrorLinkManager::HandleDeferredMirrorSave()
    {
        bool hadFailure = false;
        std::wstring lastFailure;
        while (!_deferredMirrorSaves.empty())
        {
            const UINT_PTR mirrorBufferId = _deferredMirrorSaves.front();
            _deferredMirrorSaves.pop_front();
            LinkState* link = FindLinkByMirror(mirrorBufferId);
            if (link == nullptr)
            {
                continue;
            }
            link->deferredSaveQueued = false;

            std::wstring error;
            if (!ValidateLink(*link, error, false) ||
                !SaveBufferThroughNotepad(*link, mirrorBufferId, error))
            {
                hadFailure = true;
                lastFailure = error;
                link->lastError = error;
                ShowErrorOnce(error);
                Log(L"Falha ao salvar C_: " + error);
                continue;
            }
            link->lastError.clear();
            Log(L"C_ salvo automaticamente: " + link->mirrorPath);
        }

        if (hadFailure)
        {
            UpdateToolbar(MirrorToolbarState::Error, lastFailure);
        }
        else
        {
            UpdateToolbarSummary();
        }
    }

    void MirrorLinkManager::HandleFileBeforeClose(UINT_PTR bufferId)
    {
        UnlinkForBuffer(bufferId, L"Um dos arquivos vinculados sera fechado.", false, false);
    }

    void MirrorLinkManager::HandleFileClosed(UINT_PTR bufferId)
    {
        UnlinkForBuffer(bufferId, L"Um dos arquivos vinculados deixou de estar aberto.", false, false);
    }

    void MirrorLinkManager::HandleFilePathChanged(UINT_PTR bufferId, const wchar_t* reason)
    {
        const std::wstring message = reason != nullptr
            ? std::wstring(reason)
            : L"Um dos arquivos vinculados mudou de caminho.";
        UnlinkForBuffer(bufferId, message, false, false);
    }

    void MirrorLinkManager::HandleBufferActivated()
    {
        // Um relayout de terceiros ou do proprio Notepad++ nunca deve tornar
        // visiveis os Scintillas privados usados para acessar documentos.
        KeepInternalAccessorsHidden();

        if (_isCapturingDocuments || _isApplyingMirrorUpdate)
        {
            return;
        }

        const UINT_PTR currentBuffer = static_cast<UINT_PTR>(SendMessageW(
            _nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
        if (LinkState* mirrorLink = FindLinkByMirror(currentBuffer))
        {
            SetMirrorReadOnly(*mirrorLink, true);
        }

        for (std::size_t index = _links.size(); index > 0; --index)
        {
            std::wstring error;
            if (!ValidateLink(*_links[index - 1U], error, false))
            {
                UnlinkAt(index - 1U, error, true, false);
            }
        }
    }

    void MirrorLinkManager::HandleReadOnlyChanged(UINT_PTR bufferId)
    {
        if (_isApplyingMirrorUpdate)
        {
            return;
        }
        if (LinkState* link = FindLinkByMirror(bufferId))
        {
            SetMirrorReadOnly(*link, true);
            Log(L"Protecao somente leitura reafirmada: " + link->mirrorPath);
        }
    }

    void MirrorLinkManager::HandleReadOnlyModifyAttempt(UINT_PTR bufferId)
    {
        if (_isApplyingMirrorUpdate)
        {
            return;
        }
        if (const LinkState* link = FindLinkByMirror(bufferId))
        {
            Log(L"Tentativa de edicao manual bloqueada: " + link->mirrorPath);
            UpdateToolbarSummary(
                MirrorToolbarState::Linked,
                L"O arquivo " + BaseName(link->mirrorPath) + L" esta protegido enquanto o vinculo estiver ativo.");
        }
    }

    void MirrorLinkManager::ApplyTheme()
    {
        if (_toolbar != nullptr)
        {
            _toolbar->ApplyTheme();
        }
    }

    bool MirrorLinkManager::IsApplyingMirrorUpdate() const
    {
        return _isApplyingMirrorUpdate;
    }

    bool MirrorLinkManager::IsCapturingDocuments() const
    {
        return _isCapturingDocuments;
    }

    bool MirrorLinkManager::IsLinked() const
    {
        return !_links.empty();
    }

    bool MirrorLinkManager::IsPairLinked(UINT_PTR sourceBufferId, UINT_PTR mirrorBufferId) const
    {
        return FindLinkByPair(sourceBufferId, mirrorBufferId) != nullptr;
    }

    std::size_t MirrorLinkManager::LinkCount() const
    {
        return _links.size();
    }

    UINT_PTR MirrorLinkManager::SourceBufferId() const
    {
        return _links.empty() ? 0 : _links.front()->sourceBufferId;
    }

    UINT_PTR MirrorLinkManager::MirrorBufferId() const
    {
        return _links.empty() ? 0 : _links.front()->mirrorBufferId;
    }

    bool MirrorLinkManager::CreateHiddenScintillas()
    {
        // Nunca use a janela principal do Notepad++ como pai direto dos
        // Scintillas auxiliares. Controles criados como filhos da janela
        // principal podem acabar participando de um relayout/repaint do editor
        // e aparecer sobre a area de edicao. O host abaixo e um popup privado,
        // sem WS_VISIBLE, fora da tela e que jamais participa do layout do N++.
        if (_accessorHost == nullptr || IsWindow(_accessorHost) == FALSE)
        {
            _accessorHost = CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                L"STATIC",
                L"",
                WS_POPUP | WS_DISABLED | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                -32000,
                -32000,
                1,
                1,
                _nppData._nppHandle,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);
            if (_accessorHost == nullptr)
            {
                return false;
            }
        }

        if (_sourceAccessor == nullptr || IsWindow(_sourceAccessor) == FALSE)
        {
            _sourceAccessor = reinterpret_cast<HWND>(SendMessageW(
                _nppData._nppHandle,
                NPPM_CREATESCINTILLAHANDLE,
                0,
                reinterpret_cast<LPARAM>(_accessorHost)));
        }
        if (_mirrorAccessor == nullptr || IsWindow(_mirrorAccessor) == FALSE)
        {
            _mirrorAccessor = reinterpret_cast<HWND>(SendMessageW(
                _nppData._nppHandle,
                NPPM_CREATESCINTILLAHANDLE,
                0,
                reinterpret_cast<LPARAM>(_accessorHost)));
        }

        if (_sourceAccessor == nullptr || _mirrorAccessor == nullptr)
        {
            KeepInternalAccessorsHidden();
            return false;
        }

        // Defesa adicional para versoes/configuracoes que criem o controle
        // com outro pai. Reparentar para o host privado impede que qualquer
        // rotina de layout do Notepad++ redimensione ou revele o accessor.
        if (GetParent(_sourceAccessor) != _accessorHost)
        {
            SetParent(_sourceAccessor, _accessorHost);
        }
        if (GetParent(_mirrorAccessor) != _accessorHost)
        {
            SetParent(_mirrorAccessor, _accessorHost);
        }
        KeepInternalAccessorsHidden();

        if (_sourceDefaultDocument == 0)
        {
            _sourceDefaultDocument = static_cast<sptr_t>(SendMessageW(
                _sourceAccessor, SCI_GETDOCPOINTER, 0, 0));
            if (_sourceDefaultDocument != 0)
            {
                SendMessageW(_sourceAccessor, SCI_ADDREFDOCUMENT, 0, _sourceDefaultDocument);
            }
        }
        if (_mirrorDefaultDocument == 0)
        {
            _mirrorDefaultDocument = static_cast<sptr_t>(SendMessageW(
                _mirrorAccessor, SCI_GETDOCPOINTER, 0, 0));
            if (_mirrorDefaultDocument != 0)
            {
                SendMessageW(_mirrorAccessor, SCI_ADDREFDOCUMENT, 0, _mirrorDefaultDocument);
            }
        }
        return _sourceDefaultDocument != 0 && _mirrorDefaultDocument != 0;
    }

    void MirrorLinkManager::DestroyHiddenScintillas()
    {
        DetachHiddenAccessors();
        if (_sourceAccessor != nullptr && _sourceDefaultDocument != 0)
        {
            SendMessageW(_sourceAccessor, SCI_RELEASEDOCUMENT, 0, _sourceDefaultDocument);
            _sourceDefaultDocument = 0;
        }
        if (_mirrorAccessor != nullptr && _mirrorDefaultDocument != 0)
        {
            SendMessageW(_mirrorAccessor, SCI_RELEASEDOCUMENT, 0, _mirrorDefaultDocument);
            _mirrorDefaultDocument = 0;
        }

        // NPPM_DESTROYSCINTILLAHANDLE e deprecated e nao destroi mais o
        // controle. Mantemos os accessors isolados e ocultos ate o processo
        // encerrar, quando o proprio Notepad++ libera os handles criados.
        KeepInternalAccessorsHidden();
    }

    void MirrorLinkManager::KeepInternalAccessorsHidden() const
    {
        if (_accessorHost != nullptr && IsWindow(_accessorHost) != FALSE)
        {
            SetWindowPos(
                _accessorHost,
                HWND_BOTTOM,
                -32000,
                -32000,
                1,
                1,
                SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOOWNERZORDER);
        }

        const std::array<HWND, 2> accessors = {_sourceAccessor, _mirrorAccessor};
        for (HWND accessor : accessors)
        {
            if (accessor == nullptr || IsWindow(accessor) == FALSE)
            {
                continue;
            }
            SetWindowPos(
                accessor,
                HWND_BOTTOM,
                0,
                0,
                1,
                1,
                SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOOWNERZORDER);
        }
    }

    std::vector<MirrorLinkManager::OpenDocument> MirrorLinkManager::EnumerateOpenDocuments() const
    {
        std::vector<OpenDocument> documents;
        for (int view = 0; view <= 1; ++view)
        {
            const bool viewVisible = IsViewVisible(view);
            const int viewType = view == 0 ? PRIMARY_VIEW : SECOND_VIEW;
            const int count = static_cast<int>(SendMessageW(
                _nppData._nppHandle, NPPM_GETNBOPENFILES, 0, viewType));
            for (int index = 0; index < count; ++index)
            {
                const UINT_PTR bufferId = static_cast<UINT_PTR>(SendMessageW(
                    _nppData._nppHandle, NPPM_GETBUFFERIDFROMPOS, index, view));
                if (bufferId == 0)
                {
                    continue;
                }
                const auto duplicate = std::find_if(
                    documents.begin(),
                    documents.end(),
                    [bufferId](const OpenDocument& item) { return item.bufferId == bufferId; });
                if (duplicate != documents.end())
                {
                    // Um buffer clonado pode existir nas duas views. Para
                    // qualquer operacao que precise ativar a aba, mantenha a
                    // ocorrencia visivel como representacao principal.
                    if (!duplicate->viewVisible && viewVisible)
                    {
                        duplicate->view = view;
                        duplicate->index = index;
                        duplicate->viewVisible = true;
                    }
                    continue;
                }
                const std::wstring path = GetPathFromBufferId(bufferId);
                if (!path.empty())
                {
                    documents.push_back({
                        bufferId,
                        view,
                        index,
                        path,
                        NormalizePath(path),
                        viewVisible,
                    });
                }
            }
        }
        return documents;
    }

    std::optional<MirrorLinkManager::OpenDocument> MirrorLinkManager::FindDocument(UINT_PTR bufferId) const
    {
        const auto documents = EnumerateOpenDocuments();
        const auto found = std::find_if(
            documents.begin(),
            documents.end(),
            [bufferId](const OpenDocument& item) { return item.bufferId == bufferId; });
        return found == documents.end() ? std::nullopt : std::optional<OpenDocument>(*found);
    }

    bool MirrorLinkManager::ResolvePairFromCurrent(
        OpenDocument& source,
        OpenDocument& mirror,
        std::wstring& error) const
    {
        const UINT_PTR currentBuffer = static_cast<UINT_PTR>(SendMessageW(
            _nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
        const auto current = FindDocument(currentBuffer);
        if (!current.has_value() || !current->viewVisible)
        {
            error = L"O arquivo atual nao esta disponivel.";
            return false;
        }

        const std::wstring currentName = BaseName(current->path);
        if (!IsServerName(currentName) && !IsClientName(currentName))
        {
            error = L"Ative um INI cujo nome comece com S_ ou C_.";
            return false;
        }

        std::vector<OpenDocument> matches;
        for (const OpenDocument& candidate : EnumerateOpenDocuments())
        {
            if (candidate.bufferId == current->bufferId || !candidate.viewVisible)
            {
                continue;
            }
            const std::wstring candidateName = BaseName(candidate.path);
            const bool valid = IsServerName(currentName)
                ? IsClientName(candidateName) && NamesMatch(currentName, candidateName)
                : IsServerName(candidateName) && NamesMatch(candidateName, currentName);
            if (valid)
            {
                matches.push_back(candidate);
            }
        }
        if (matches.size() != 1U)
        {
            error = matches.empty()
                ? L"O arquivo espelhado correspondente precisa estar aberto em uma view visivel."
                : L"Ha mais de um arquivo correspondente aberto; selecione o par pela lista de vinculos.";
            return false;
        }

        if (IsServerName(currentName))
        {
            source = *current;
            mirror = matches.front();
        }
        else
        {
            source = matches.front();
            mirror = *current;
        }
        return true;
    }

    bool MirrorLinkManager::CaptureDocumentPointers(
        LinkState& link,
        const OpenDocument& source,
        const OpenDocument& mirror,
        std::wstring& error)
    {
        const UINT_PTR originalBuffer = static_cast<UINT_PTR>(SendMessageW(
            _nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
        const std::array<UINT_PTR, 2> originalViewBuffers = {
            GetActiveBufferInView(0),
            GetActiveBufferInView(1),
        };
        int originalView = 0;
        SendMessageW(
            _nppData._nppHandle,
            NPPM_GETCURRENTSCINTILLA,
            0,
            reinterpret_cast<LPARAM>(&originalView));
        const HWND originalFocus = GetFocus();

        _isCapturingDocuments = true;
        bool success = CaptureDocumentPointer(
            source,
            link.sourceDocument,
            nullptr,
            link.sourceEncoding,
            error);
        if (success)
        {
            success = CaptureDocumentPointer(
                mirror,
                link.mirrorDocument,
                &link.mirrorWasReadOnly,
                link.mirrorEncoding,
                error);
        }
        RestoreOpenDocuments(
            originalViewBuffers,
            originalBuffer,
            originalView,
            originalFocus);
        _isCapturingDocuments = false;

        if (!success)
        {
            ReleaseLinkDocuments(link);
            return false;
        }
        if (!AttachAccessors(link, error))
        {
            return false;
        }
        // Um documento que ja estava modificado antes do vinculo ja e
        // conhecido pelo Notepad++. As alteracoes internas futuras usam
        // NPPM_MAKECURRENTBUFFERDIRTY quando este estado estiver limpo.
        link.mirrorDirtyRegisteredWithNotepad =
            SendMessageW(_mirrorAccessor, SCI_GETMODIFY, 0, 0) != FALSE;
        return true;
    }

    bool MirrorLinkManager::CaptureDocumentPointer(
        const OpenDocument& document,
        sptr_t& documentPointer,
        bool* visibleReadOnly,
        MirrorDocumentEncoding& encoding,
        std::wstring& error)
    {
        if (!IsViewVisible(document.view))
        {
            error = L"A view do documento esta oculta. Ative a view antes de vincular: " + document.path;
            return false;
        }
        if (!ActivateBufferInView(document.bufferId, document.view))
        {
            error = L"Nao foi possivel acessar o documento aberto: " + document.path;
            return false;
        }
        const HWND visibleScintilla = ScintillaForView(document.view);
        if (visibleScintilla == nullptr)
        {
            error = L"A view do documento nao esta disponivel: " + document.path;
            return false;
        }
        if (visibleReadOnly != nullptr)
        {
            *visibleReadOnly = SendMessageW(visibleScintilla, SCI_GETREADONLY, 0, 0) != FALSE;
        }
        encoding = CaptureActiveDocumentEncoding(visibleScintilla, document.bufferId);
        documentPointer = static_cast<sptr_t>(SendMessageW(
            visibleScintilla, SCI_GETDOCPOINTER, 0, 0));
        if (documentPointer == 0)
        {
            error = L"O Notepad++ nao retornou o documento Scintilla de: " + document.path;
            return false;
        }
        SendMessageW(visibleScintilla, SCI_ADDREFDOCUMENT, 0, documentPointer);
        return true;
    }

    bool MirrorLinkManager::AttachAccessors(const LinkState& link, std::wstring& error) const
    {
        if (_sourceAccessor == nullptr || _mirrorAccessor == nullptr ||
            link.sourceDocument == 0 || link.mirrorDocument == 0)
        {
            error = L"Os acessores internos do vinculo nao estao disponiveis.";
            return false;
        }

        SendMessageW(_sourceAccessor, SCI_SETDOCPOINTER, 0, link.sourceDocument);
        SendMessageW(_mirrorAccessor, SCI_SETDOCPOINTER, 0, link.mirrorDocument);
        KeepInternalAccessorsHidden();
        if (static_cast<sptr_t>(SendMessageW(_sourceAccessor, SCI_GETDOCPOINTER, 0, 0)) != link.sourceDocument ||
            static_cast<sptr_t>(SendMessageW(_mirrorAccessor, SCI_GETDOCPOINTER, 0, 0)) != link.mirrorDocument)
        {
            error = L"Nao foi possivel acessar os documentos internos do vinculo.";
            return false;
        }

        const UINT sourceCodePage = static_cast<UINT>(SendMessageW(_sourceAccessor, SCI_GETCODEPAGE, 0, 0));
        const UINT mirrorCodePage = static_cast<UINT>(SendMessageW(_mirrorAccessor, SCI_GETCODEPAGE, 0, 0));
        if (sourceCodePage != link.sourceEncoding.internalCodePage ||
            mirrorCodePage != link.mirrorEncoding.internalCodePage)
        {
            error = L"A code page interna de um arquivo vinculado mudou. O vinculo foi interrompido por seguranca.";
            return false;
        }
        return true;
    }

    void MirrorLinkManager::DetachHiddenAccessors() const
    {
        if (_sourceAccessor != nullptr && _sourceDefaultDocument != 0)
        {
            SendMessageW(_sourceAccessor, SCI_SETDOCPOINTER, 0, _sourceDefaultDocument);
        }
        if (_mirrorAccessor != nullptr && _mirrorDefaultDocument != 0)
        {
            SendMessageW(_mirrorAccessor, SCI_SETDOCPOINTER, 0, _mirrorDefaultDocument);
        }
    }

    void MirrorLinkManager::ReleaseLinkDocuments(LinkState& link)
    {
        DetachHiddenAccessors();
        if (_sourceAccessor != nullptr && link.sourceDocument != 0)
        {
            SendMessageW(_sourceAccessor, SCI_RELEASEDOCUMENT, 0, link.sourceDocument);
            link.sourceDocument = 0;
        }
        if (_mirrorAccessor != nullptr && link.mirrorDocument != 0)
        {
            SendMessageW(_mirrorAccessor, SCI_RELEASEDOCUMENT, 0, link.mirrorDocument);
            link.mirrorDocument = 0;
        }
    }

    bool MirrorLinkManager::ValidateLink(
        const LinkState& link,
        std::wstring& error,
        bool requireFilesOnDisk) const
    {
        const auto source = FindDocument(link.sourceBufferId);
        const auto mirror = FindDocument(link.mirrorBufferId);
        if (!source.has_value() || !mirror.has_value())
        {
            error = L"Um dos arquivos vinculados nao esta mais aberto.";
            return false;
        }
        if (source->normalizedPath != link.sourceNormalizedPath ||
            mirror->normalizedPath != link.mirrorNormalizedPath)
        {
            error = L"Um dos arquivos vinculados foi renomeado ou mudou de caminho.";
            return false;
        }
        if (requireFilesOnDisk &&
            (GetFileAttributesW(link.sourcePath.c_str()) == INVALID_FILE_ATTRIBUTES ||
             GetFileAttributesW(link.mirrorPath.c_str()) == INVALID_FILE_ATTRIBUTES))
        {
            error = L"Um dos arquivos vinculados deixou de estar disponivel no disco.";
            return false;
        }

        const int sourceMode = static_cast<int>(SendMessageW(
            _nppData._nppHandle, NPPM_GETBUFFERENCODING, link.sourceBufferId, 0));
        const int mirrorMode = static_cast<int>(SendMessageW(
            _nppData._nppHandle, NPPM_GETBUFFERENCODING, link.mirrorBufferId, 0));
        if (sourceMode != link.sourceEncoding.unicodeMode)
        {
            error = L"O encoding do S_ mudou depois da vinculacao.";
            return false;
        }
        if (mirrorMode != link.mirrorEncoding.unicodeMode)
        {
            error = L"O encoding do C_ mudou depois da vinculacao.";
            return false;
        }

        const UINT_PTR currentBuffer = static_cast<UINT_PTR>(SendMessageW(
            _nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
        if (currentBuffer == link.sourceBufferId || currentBuffer == link.mirrorBufferId)
        {
            int activeView = 0;
            SendMessageW(
                _nppData._nppHandle,
                NPPM_GETCURRENTSCINTILLA,
                0,
                reinterpret_cast<LPARAM>(&activeView));
            const MirrorDocumentEncoding currentEncoding = CaptureActiveDocumentEncoding(
                ScintillaForView(activeView),
                currentBuffer);
            const MirrorDocumentEncoding& expected = currentBuffer == link.sourceBufferId
                ? link.sourceEncoding
                : link.mirrorEncoding;
            if (currentEncoding != expected)
            {
                error = L"A code page do arquivo vinculado mudou. O vinculo foi interrompido para evitar corrupcao.";
                return false;
            }
        }
        return true;
    }

    bool MirrorLinkManager::SynchronizeImmediate(
        LinkState& link,
        const wchar_t* reason,
        bool showError,
        std::wstring* errorOut,
        bool registerMirrorDirty)
    {
        if (errorOut != nullptr)
        {
            errorOut->clear();
        }
        std::wstring error;
        if (!ValidateLink(link, error) || !AttachAccessors(link, error))
        {
            if (errorOut != nullptr)
            {
                *errorOut = error;
            }
            link.lastError = error;
            UpdateToolbar(MirrorToolbarState::Error, error);
            if (showError)
            {
                ShowErrorOnce(error);
            }
            return false;
        }

        const auto started = std::chrono::steady_clock::now();
        Log(L"Sincronizacao iniciada (" + std::wstring(reason) + L"): " + link.sourcePath);
        MirrorSyncWorker::Job job{};
        job.generation = link.generation = ++_nextGeneration;
        // Uma sincronizacao imediata (criacao, Sync ou save) invalida qualquer
        // debounce pendente para este S_. Jobs antigos tambem sao descartados
        // pela geracao acima.
        _dirtySources.erase(link.sourceBufferId);
        if (_dirtySources.empty())
        {
            KillTimer(_ownerWindow, MirrorSyncTimerId);
        }
        job.sourceBufferId = link.sourceBufferId;
        job.mirrorBufferId = link.mirrorBufferId;
        job.mirrorEncoding = link.mirrorEncoding;
        if (!GetDocumentUtf8(_sourceAccessor, job.sourceUtf8, error) ||
            !GetDocumentUtf8(_mirrorAccessor, job.mirrorUtf8, error))
        {
            if (errorOut != nullptr)
            {
                *errorOut = error;
            }
            link.lastError = error;
            UpdateToolbar(MirrorToolbarState::Error, error);
            if (showError)
            {
                ShowErrorOnce(error);
            }
            return false;
        }

        MirrorSyncWorkerResult converted = MirrorSyncWorker::Convert(job);
        if (!converted.success)
        {
            if (errorOut != nullptr)
            {
                *errorOut = converted.error;
            }
            link.lastError = converted.error;
            UpdateToolbar(MirrorToolbarState::Error, converted.error);
            if (showError)
            {
                ShowErrorOnce(converted.error);
            }
            Log(L"Falha de conversao de encoding: " + converted.error);
            return false;
        }
        if (converted.different &&
            !ApplyConvertedMirrorText(
                link,
                converted.targetBytes,
                converted.sourceCharacters,
                converted.conversionMilliseconds,
                &error,
                registerMirrorDirty))
        {
            if (errorOut != nullptr)
            {
                *errorOut = error;
            }
            link.lastError = error;
            if (showError)
            {
                ShowErrorOnce(error);
            }
            return false;
        }

        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        Log(L"Sincronizacao concluida em " + std::to_wstring(milliseconds) + L" ms: " + link.sourcePath);
        link.lastError.clear();
        _errorMessageShown = false;
        return true;
    }

    void MirrorLinkManager::SubmitBackgroundSync(LinkState& link)
    {
        if (!_worker)
        {
            return;
        }
        std::wstring error;
        if (!ValidateLink(link, error, false) || !AttachAccessors(link, error))
        {
            link.lastError = error;
            UpdateToolbar(MirrorToolbarState::Error, error);
            return;
        }

        MirrorSyncWorker::Job job{};
        job.generation = link.generation;
        job.sourceBufferId = link.sourceBufferId;
        job.mirrorBufferId = link.mirrorBufferId;
        job.mirrorEncoding = link.mirrorEncoding;
        if (!GetDocumentUtf8(_sourceAccessor, job.sourceUtf8, error) ||
            !GetDocumentUtf8(_mirrorAccessor, job.mirrorUtf8, error))
        {
            link.lastError = error;
            UpdateToolbar(MirrorToolbarState::Error, error);
            return;
        }
        UpdateToolbarSummary(MirrorToolbarState::Syncing);
        _worker->Submit(std::move(job));
    }

    bool MirrorLinkManager::ApplyConvertedMirrorText(
        LinkState& link,
        const std::string& targetBytes,
        std::size_t sourceCharacters,
        double conversionMilliseconds,
        std::wstring* errorOut,
        bool registerMirrorDirty)
    {
        if (errorOut != nullptr)
        {
            errorOut->clear();
        }
        std::wstring error;
        if (!AttachAccessors(link, error))
        {
            if (errorOut != nullptr)
            {
                *errorOut = error;
            }
            return false;
        }

        const bool oldModified = SendMessageW(_mirrorAccessor, SCI_GETMODIFY, 0, 0) != FALSE;
        const LRESULT oldLength = SendMessageW(_mirrorAccessor, SCI_GETLENGTH, 0, 0);
        if (oldLength < 0 || oldLength > MaxMirrorDocumentLength)
        {
            error = L"O C_ ultrapassa o limite seguro de 512 MB.";
            if (errorOut != nullptr)
            {
                *errorOut = error;
            }
            return false;
        }
        std::string oldBytes(static_cast<std::size_t>(oldLength) + 1U, '\0');
        SendMessageW(
            _mirrorAccessor,
            SCI_GETTEXT,
            static_cast<WPARAM>(oldBytes.size()),
            reinterpret_cast<LPARAM>(oldBytes.data()));
        oldBytes.resize(static_cast<std::size_t>(oldLength));

        const auto states = CaptureVisibleMirrorStates(link);
        for (const ViewState& state : states)
        {
            if (state.scintilla != nullptr && IsWindow(state.scintilla) != FALSE)
            {
                SendMessageW(state.scintilla, WM_SETREDRAW, FALSE, 0);
            }
        }

        const bool previousApplying = _isApplyingMirrorUpdate;
        _isApplyingMirrorUpdate = true;
        SetMirrorReadOnly(link, false);
        SendMessageW(
            _mirrorAccessor,
            SCI_SETTEXT,
            0,
            reinterpret_cast<LPARAM>(targetBytes.c_str()));

        const LRESULT newLength = SendMessageW(_mirrorAccessor, SCI_GETLENGTH, 0, 0);
        bool exact = newLength == static_cast<LRESULT>(targetBytes.size());
        if (exact)
        {
            std::string verification(targetBytes.size() + 1U, '\0');
            SendMessageW(
                _mirrorAccessor,
                SCI_GETTEXT,
                static_cast<WPARAM>(verification.size()),
                reinterpret_cast<LPARAM>(verification.data()));
            verification.resize(targetBytes.size());
            exact = verification == targetBytes;
        }

        if (!exact)
        {
            SendMessageW(
                _mirrorAccessor,
                SCI_SETTEXT,
                0,
                reinterpret_cast<LPARAM>(oldBytes.c_str()));
            if (!oldModified)
            {
                SendMessageW(_mirrorAccessor, SCI_SETSAVEPOINT, 0, 0);
            }
            error = L"A verificacao da copia no C_ falhou. O conteudo anterior foi restaurado.";
        }
        SetMirrorReadOnly(link, true);
        _isApplyingMirrorUpdate = previousApplying;
        RestoreVisibleMirrorStates(states);

        // Reabilita o desenho sem forcar paint no meio da operacao. A
        // invalidacao ocorre apenas depois que o estado dirty do Notepad++
        // tambem estiver coerente, evitando piscar ou mostrar outra aba.
        for (const ViewState& state : states)
        {
            if (state.scintilla != nullptr && IsWindow(state.scintilla) != FALSE)
            {
                SendMessageW(state.scintilla, WM_SETREDRAW, TRUE, 0);
            }
        }

        if (!exact)
        {
            for (const ViewState& state : states)
            {
                if (state.scintilla != nullptr && IsWindow(state.scintilla) != FALSE)
                {
                    RedrawWindow(state.scintilla, nullptr, nullptr, RDW_INVALIDATE);
                }
            }
            if (errorOut != nullptr)
            {
                *errorOut = error;
            }
            UpdateToolbar(MirrorToolbarState::Error, error);
            Log(error);
            return false;
        }

        if (registerMirrorDirty && !link.mirrorDirtyRegisteredWithNotepad)
        {
            if (!MarkMirrorDirtyThroughNotepad(link, error))
            {
                for (const ViewState& state : states)
                {
                    if (state.scintilla != nullptr && IsWindow(state.scintilla) != FALSE)
                    {
                        RedrawWindow(state.scintilla, nullptr, nullptr, RDW_INVALIDATE);
                    }
                }
                if (errorOut != nullptr)
                {
                    *errorOut = error;
                }
                UpdateToolbar(MirrorToolbarState::Error, error);
                Log(error);
                return false;
            }
            link.mirrorDirtyRegisteredWithNotepad = true;
        }

        for (const ViewState& state : states)
        {
            if (state.scintilla != nullptr && IsWindow(state.scintilla) != FALSE)
            {
                RedrawWindow(state.scintilla, nullptr, nullptr, RDW_INVALIDATE);
            }
        }

        Log((registerMirrorDirty
                ? L"Diferenca aplicada e reconhecida pelo Notepad++ como nao salva: "
                : L"Diferenca aplicada antes do save; dirty state sera registrado na fila do C_: ") +
            std::to_wstring(targetBytes.size()) + L" bytes / " +
            std::to_wstring(sourceCharacters) + L" caracteres; conversao " +
            std::to_wstring(conversionMilliseconds) + L" ms. Destino: " + link.mirrorPath);
        return true;
    }

    bool MirrorLinkManager::MarkMirrorDirtyThroughNotepad(
        LinkState& link,
        std::wstring& error)
    {
        error.clear();
        if (!ValidateLink(link, error, false) || !AttachAccessors(link, error))
        {
            return false;
        }

        // NPPM_MAKECURRENTBUFFERDIRTY atua somente sobre o buffer atual.
        // A ativacao abaixo e totalmente interna: desenho, foco, abas, cursor,
        // selecoes, scroll horizontal/vertical e zoom sao restaurados antes de
        // devolver o controle ao usuario.
        const auto mirrorDocument = FindDocument(link.mirrorBufferId);
        if (!mirrorDocument.has_value())
        {
            error = L"O arquivo C_ nao esta mais aberto: " + link.mirrorPath;
            return false;
        }

        const UINT_PTR originalBuffer = static_cast<UINT_PTR>(SendMessageW(
            _nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
        const std::array<UINT_PTR, 2> originalViewBuffers = {
            GetActiveBufferInView(0),
            GetActiveBufferInView(1),
        };
        int originalView = 0;
        SendMessageW(
            _nppData._nppHandle,
            NPPM_GETCURRENTSCINTILLA,
            0,
            reinterpret_cast<LPARAM>(&originalView));
        const HWND originalFocus = GetFocus();
        const auto viewStates = CaptureActiveViewStates();

        std::vector<HWND> redrawWindows;
        redrawWindows.reserve(3U);
        if (_nppData._nppHandle != nullptr)
        {
            redrawWindows.push_back(_nppData._nppHandle);
        }
        for (int view = 0; view <= 1; ++view)
        {
            if (IsViewVisible(view))
            {
                redrawWindows.push_back(ScintillaForView(view));
            }
        }
        for (HWND window : redrawWindows)
        {
            if (window != nullptr && IsWindow(window) != FALSE)
            {
                SendMessageW(window, WM_SETREDRAW, FALSE, 0);
            }
        }

        const bool previousApplying = _isApplyingMirrorUpdate;
        const bool previousCapturing = _isCapturingDocuments;
        _isApplyingMirrorUpdate = true;
        _isCapturingDocuments = true;

        bool activated = originalBuffer == link.mirrorBufferId;
        if (!activated)
        {
            activated = ActivateBufferInView(link.mirrorBufferId, mirrorDocument->view);
        }

        LRESULT dirtyResult = FALSE;
        if (activated)
        {
            const UINT_PTR activeBuffer = static_cast<UINT_PTR>(SendMessageW(
                _nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
            if (activeBuffer == link.mirrorBufferId)
            {
                dirtyResult = SendMessageW(
                    _nppData._nppHandle,
                    NPPM_MAKECURRENTBUFFERDIRTY,
                    0,
                    0);
            }
        }

        RestoreOpenDocuments(
            originalViewBuffers,
            originalBuffer,
            originalView,
            originalFocus);
        RestoreVisibleMirrorStates(viewStates);
        _isCapturingDocuments = previousCapturing;
        _isApplyingMirrorUpdate = previousApplying;

        for (HWND window : redrawWindows)
        {
            if (window != nullptr && IsWindow(window) != FALSE)
            {
                SendMessageW(window, WM_SETREDRAW, TRUE, 0);
            }
        }
        if (_nppData._nppHandle != nullptr && IsWindow(_nppData._nppHandle) != FALSE)
        {
            RedrawWindow(
                _nppData._nppHandle,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_ALLCHILDREN);
        }

        const bool scintillaModified =
            SendMessageW(_mirrorAccessor, SCI_GETMODIFY, 0, 0) != FALSE;
        if (!activated || dirtyResult == FALSE || !scintillaModified)
        {
            error = L"O conteudo do C_ foi atualizado, mas o Notepad++ nao conseguiu "
                L"marcar a aba como nao salva: " + link.mirrorPath;
            Log(L"Falha ao registrar dirty state do C_. ativado=" +
                std::to_wstring(activated ? 1 : 0) +
                L", retorno=" + std::to_wstring(dirtyResult) +
                L", SCI_GETMODIFY=" + std::to_wstring(scintillaModified ? 1 : 0) +
                L", caminho=" + link.mirrorPath);
            return false;
        }

        Log(L"C_ marcado como nao salvo pelo Notepad++: " + link.mirrorPath);
        return true;
    }

    bool MirrorLinkManager::SaveBufferThroughNotepad(
        LinkState& link,
        UINT_PTR bufferId,
        std::wstring& error)
    {
        error.clear();
        if (!ValidateLink(link, error, false) || !AttachAccessors(link, error))
        {
            return false;
        }

        const bool isMirror = bufferId == link.mirrorBufferId;
        const bool isSource = bufferId == link.sourceBufferId;
        if (!isMirror && !isSource)
        {
            error = L"O arquivo solicitado nao pertence ao vinculo.";
            return false;
        }

        const std::wstring& path = isMirror ? link.mirrorPath : link.sourcePath;
        HWND accessor = isMirror ? _mirrorAccessor : _sourceAccessor;
        const MirrorDocumentEncoding& expectedEncoding = isMirror
            ? link.mirrorEncoding
            : link.sourceEncoding;

        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_READONLY) != 0)
        {
            error = L"O Windows marcou o arquivo como somente leitura e o Notepad++ nao pode salva-lo: " + path;
            return false;
        }
        if (SendMessageW(accessor, SCI_GETMODIFY, 0, 0) == FALSE &&
            attributes != INVALID_FILE_ATTRIBUTES)
        {
            if (isMirror)
            {
                link.mirrorDirtyRegisteredWithNotepad = false;
            }
            return true;
        }
        if (isMirror && !link.mirrorDirtyRegisteredWithNotepad)
        {
            if (!MarkMirrorDirtyThroughNotepad(link, error))
            {
                return false;
            }
            link.mirrorDirtyRegisteredWithNotepad = true;
        }

        const UINT_PTR originalBuffer = static_cast<UINT_PTR>(SendMessageW(
            _nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
        const std::array<UINT_PTR, 2> originalViewBuffers = {
            GetActiveBufferInView(0),
            GetActiveBufferInView(1),
        };
        int originalView = 0;
        SendMessageW(
            _nppData._nppHandle,
            NPPM_GETCURRENTSCINTILLA,
            0,
            reinterpret_cast<LPARAM>(&originalView));
        const HWND originalFocus = GetFocus();
        const auto viewStates = CaptureActiveViewStates();

        const bool previousApplying = _isApplyingMirrorUpdate;
        _isApplyingMirrorUpdate = true;
        _internalSaveBuffers.insert(bufferId);
        if (isMirror)
        {
            SetMirrorReadOnly(link, false);
        }

        // NPPM_SAVEFILE e a API nativa do Notepad++ para salvar um arquivo
        // aberto pelo caminho completo sem depender da aba ativa.
        const LRESULT pathSaveResult = SendMessageW(
            _nppData._nppHandle,
            NPPM_SAVEFILE,
            0,
            reinterpret_cast<LPARAM>(path.c_str()));
        // O retorno de algumas versoes/configuracoes pode ser FALSE mesmo
        // quando o savepoint foi atingido. O estado SCI_GETMODIFY do proprio
        // documento e a verificacao final confiavel para nao gerar falso erro.
        bool saved = SendMessageW(accessor, SCI_GETMODIFY, 0, 0) == FALSE;
        LRESULT currentSaveResult = FALSE;
        bool activatedForFallback = false;

        // Fallback ainda nativo: caso o caminho nao seja reconhecido por uma
        // configuracao/versao especifica, ativa o BufferID exato, salva pelo
        // comando Save Current File e restaura view, abas, foco e estado visual.
        // O redraw so e suspenso quando essa ativacao realmente e necessaria.
        if (!saved)
        {
            std::vector<HWND> redrawWindows;
            redrawWindows.reserve(3U);
            if (_nppData._nppHandle != nullptr)
            {
                redrawWindows.push_back(_nppData._nppHandle);
            }
            for (int view = 0; view <= 1; ++view)
            {
                if (IsViewVisible(view))
                {
                    redrawWindows.push_back(ScintillaForView(view));
                }
            }
            for (HWND window : redrawWindows)
            {
                if (window != nullptr && IsWindow(window) != FALSE)
                {
                    SendMessageW(window, WM_SETREDRAW, FALSE, 0);
                }
            }

            activatedForFallback = ActivateBuffer(bufferId);
            if (activatedForFallback)
            {
                int activeView = 0;
                SendMessageW(
                    _nppData._nppHandle,
                    NPPM_GETCURRENTSCINTILLA,
                    0,
                    reinterpret_cast<LPARAM>(&activeView));
                if (isMirror)
                {
                    SendMessageW(ScintillaForView(activeView), SCI_SETREADONLY, FALSE, 0);
                }
                currentSaveResult = SendMessageW(
                    _nppData._nppHandle,
                    NPPM_SAVECURRENTFILE,
                    0,
                    0);
                saved = SendMessageW(accessor, SCI_GETMODIFY, 0, 0) == FALSE;
            }

            if (activatedForFallback)
            {
                RestoreOpenDocuments(
                    originalViewBuffers,
                    originalBuffer,
                    originalView,
                    originalFocus);
                RestoreVisibleMirrorStates(viewStates);
            }
            for (HWND window : redrawWindows)
            {
                if (window != nullptr && IsWindow(window) != FALSE)
                {
                    SendMessageW(window, WM_SETREDRAW, TRUE, 0);
                }
            }
            if (_nppData._nppHandle != nullptr && IsWindow(_nppData._nppHandle) != FALSE)
            {
                RedrawWindow(
                    _nppData._nppHandle,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
        }

        if (isMirror)
        {
            SetMirrorReadOnly(link, true);
        }
        _internalSaveBuffers.erase(bufferId);
        _isApplyingMirrorUpdate = previousApplying;

        const bool existsAfter = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
        const int encodingModeAfter = static_cast<int>(SendMessageW(
            _nppData._nppHandle,
            NPPM_GETBUFFERENCODING,
            bufferId,
            0));
        const UINT internalCodePageAfter = static_cast<UINT>(SendMessageW(
            accessor,
            SCI_GETCODEPAGE,
            0,
            0));
        if (encodingModeAfter != expectedEncoding.unicodeMode ||
            internalCodePageAfter != expectedEncoding.internalCodePage)
        {
            error = L"O encoding interno mudou durante o salvamento de " + BaseName(path) +
                L". O vinculo foi interrompido por seguranca.";
            Log(error);
            return false;
        }
        if (!saved || !existsAfter)
        {
            error = L"O Notepad++ nao conseguiu salvar " + BaseName(path) +
                L". O documento continua modificado e o encoding original foi preservado.";
            Log(L"Save falhou. NPPM_SAVEFILE=" + std::to_wstring(pathSaveResult) +
                L", NPPM_SAVECURRENTFILE=" + std::to_wstring(currentSaveResult) +
                L", modificado=" + std::to_wstring(SendMessageW(accessor, SCI_GETMODIFY, 0, 0)) +
                L", encoding=" + EncodingName(expectedEncoding) +
                L", caminho=" + path);
            return false;
        }

        if (isMirror)
        {
            link.mirrorDirtyRegisteredWithNotepad = false;
        }
        Log(L"Arquivo salvo pelo Notepad++: " + path +
            L"; encoding preservado: " + EncodingName(expectedEncoding));
        return true;
    }

    bool MirrorLinkManager::SavePair(LinkState& link, std::wstring& error)
    {
        return SaveBufferThroughNotepad(link, link.sourceBufferId, error) &&
            SaveBufferThroughNotepad(link, link.mirrorBufferId, error);
    }

    void MirrorLinkManager::UnlinkAt(
        std::size_t index,
        const std::wstring& reason,
        bool errorState,
        bool showError)
    {
        if (index >= _links.size())
        {
            return;
        }
        LinkState& link = *_links[index];
        _dirtySources.erase(link.sourceBufferId);
        _deferredMirrorSaves.erase(
            std::remove(_deferredMirrorSaves.begin(), _deferredMirrorSaves.end(), link.mirrorBufferId),
            _deferredMirrorSaves.end());
        _internalSaveBuffers.erase(link.sourceBufferId);
        _internalSaveBuffers.erase(link.mirrorBufferId);

        const bool previousApplying = _isApplyingMirrorUpdate;
        _isApplyingMirrorUpdate = true;
        SetMirrorReadOnly(link, link.mirrorWasReadOnly);
        _isApplyingMirrorUpdate = previousApplying;
        Log(L"Vinculo removido: " + link.sourcePath + L" -> " + link.mirrorPath + L". Motivo: " + reason);
        ReleaseLinkDocuments(link);
        _links.erase(_links.begin() + static_cast<std::ptrdiff_t>(index));

        if (errorState)
        {
            UpdateToolbar(MirrorToolbarState::Error, reason);
            if (showError)
            {
                ShowErrorOnce(reason);
            }
        }
        else
        {
            _errorMessageShown = false;
            UpdateToolbarSummary();
        }
    }

    void MirrorLinkManager::UnlinkForBuffer(
        UINT_PTR bufferId,
        const std::wstring& reason,
        bool errorState,
        bool showError)
    {
        for (std::size_t index = _links.size(); index > 0; --index)
        {
            const LinkState& link = *_links[index - 1U];
            if (link.sourceBufferId == bufferId || link.mirrorBufferId == bufferId)
            {
                UnlinkAt(index - 1U, reason, errorState, showError);
            }
        }
    }

    void MirrorLinkManager::SetMirrorReadOnly(const LinkState& link, bool readOnly) const
    {
        std::wstring ignored;
        if (AttachAccessors(link, ignored))
        {
            SendMessageW(_mirrorAccessor, SCI_SETREADONLY, readOnly ? TRUE : FALSE, 0);
        }
        for (int view = 0; view <= 1; ++view)
        {
            if (!IsViewVisible(view))
            {
                continue;
            }
            if (GetActiveBufferInView(view) == link.mirrorBufferId)
            {
                SendMessageW(
                    ScintillaForView(view),
                    SCI_SETREADONLY,
                    readOnly ? TRUE : FALSE,
                    0);
            }
        }
    }

    MirrorLinkManager::ViewState MirrorLinkManager::CaptureViewState(HWND scintilla) const
    {
        ViewState state{};
        state.scintilla = scintilla;
        if (scintilla == nullptr || IsWindow(scintilla) == FALSE)
        {
            return state;
        }

        const int selectionCount = std::max(
            1,
            static_cast<int>(SendMessageW(scintilla, SCI_GETSELECTIONS, 0, 0)));
        state.selections.reserve(static_cast<std::size_t>(selectionCount));
        for (int selection = 0; selection < selectionCount; ++selection)
        {
            state.selections.push_back({
                static_cast<Sci_Position>(SendMessageW(scintilla, SCI_GETSELECTIONNCARET, selection, 0)),
                static_cast<Sci_Position>(SendMessageW(scintilla, SCI_GETSELECTIONNANCHOR, selection, 0)),
            });
        }
        state.mainSelection = static_cast<int>(SendMessageW(scintilla, SCI_GETMAINSELECTION, 0, 0));
        state.firstVisibleLine = static_cast<Sci_Position>(SendMessageW(scintilla, SCI_GETFIRSTVISIBLELINE, 0, 0));
        state.xOffset = static_cast<int>(SendMessageW(scintilla, SCI_GETXOFFSET, 0, 0));
        state.zoom = static_cast<int>(SendMessageW(scintilla, SCI_GETZOOM, 0, 0));
        return state;
    }

    std::vector<MirrorLinkManager::ViewState> MirrorLinkManager::CaptureActiveViewStates() const
    {
        std::vector<ViewState> states;
        states.reserve(2U);
        for (int view = 0; view <= 1; ++view)
        {
            if (!IsViewVisible(view) || GetActiveBufferInView(view) == 0)
            {
                continue;
            }
            states.push_back(CaptureViewState(ScintillaForView(view)));
        }
        return states;
    }

    std::vector<MirrorLinkManager::ViewState> MirrorLinkManager::CaptureVisibleMirrorStates(
        const LinkState& link) const
    {
        std::vector<ViewState> states;
        for (int view = 0; view <= 1; ++view)
        {
            if (!IsViewVisible(view))
            {
                continue;
            }
            if (GetActiveBufferInView(view) == link.mirrorBufferId)
            {
                states.push_back(CaptureViewState(ScintillaForView(view)));
            }
        }
        return states;
    }

    void MirrorLinkManager::RestoreVisibleMirrorStates(const std::vector<ViewState>& states) const
    {
        for (const ViewState& state : states)
        {
            if (state.scintilla == nullptr || state.selections.empty())
            {
                continue;
            }
            const Sci_Position length = static_cast<Sci_Position>(SendMessageW(
                state.scintilla, SCI_GETLENGTH, 0, 0));
            const auto clampPosition = [length](Sci_Position position)
            {
                return std::max<Sci_Position>(0, std::min(position, length));
            };
            SendMessageW(state.scintilla, SCI_CLEARSELECTIONS, 0, 0);
            SendMessageW(
                state.scintilla,
                SCI_SETSELECTION,
                static_cast<WPARAM>(clampPosition(state.selections.front().caret)),
                static_cast<LPARAM>(clampPosition(state.selections.front().anchor)));
            for (std::size_t index = 1; index < state.selections.size(); ++index)
            {
                SendMessageW(
                    state.scintilla,
                    SCI_ADDSELECTION,
                    static_cast<WPARAM>(clampPosition(state.selections[index].caret)),
                    static_cast<LPARAM>(clampPosition(state.selections[index].anchor)));
            }
            SendMessageW(
                state.scintilla,
                SCI_SETMAINSELECTION,
                static_cast<WPARAM>(std::clamp(
                    state.mainSelection,
                    0,
                    static_cast<int>(state.selections.size()) - 1)),
                0);
            SendMessageW(state.scintilla, SCI_SETFIRSTVISIBLELINE, state.firstVisibleLine, 0);
            SendMessageW(state.scintilla, SCI_SETXOFFSET, state.xOffset, 0);
            SendMessageW(state.scintilla, SCI_SETZOOM, state.zoom, 0);
        }
    }

    UINT_PTR MirrorLinkManager::GetActiveBufferInView(int view) const
    {
        if (!IsViewVisible(view))
        {
            return 0;
        }
        const int index = static_cast<int>(SendMessageW(
            _nppData._nppHandle, NPPM_GETCURRENTDOCINDEX, 0, view));
        if (index < 0)
        {
            return 0;
        }
        return static_cast<UINT_PTR>(SendMessageW(
            _nppData._nppHandle, NPPM_GETBUFFERIDFROMPOS, index, view));
    }

    bool MirrorLinkManager::IsViewVisible(int view) const
    {
        if (view < 0 || view > 1 || _nppData._nppHandle == nullptr)
        {
            return false;
        }

        // A API do Notepad++ retorna -1 quando a view esta oculta. Esse teste
        // e mais confiavel do que assumir que o segundo HWND esta utilizavel,
        // pois o handle existe mesmo quando a sub-view nao participa do layout.
        const int index = static_cast<int>(SendMessageW(
            _nppData._nppHandle,
            NPPM_GETCURRENTDOCINDEX,
            0,
            view));
        if (index < 0)
        {
            return false;
        }

        const HWND scintilla = ScintillaForView(view);
        return scintilla != nullptr && IsWindow(scintilla) != FALSE;
    }

    HWND MirrorLinkManager::ScintillaForView(int view) const
    {
        return view == 1 ? _nppData._scintillaSecondHandle : _nppData._scintillaMainHandle;
    }

    int MirrorLinkManager::GetBufferPosition(UINT_PTR bufferId, int priorityView) const
    {
        return static_cast<int>(SendMessageW(
            _nppData._nppHandle, NPPM_GETPOSFROMBUFFERID, bufferId, priorityView));
    }

    bool MirrorLinkManager::ActivateBuffer(UINT_PTR bufferId) const
    {
        return ActivateBufferInView(bufferId, 0);
    }

    bool MirrorLinkManager::ActivateBufferInView(UINT_PTR bufferId, int priorityView) const
    {
        const int packed = GetBufferPosition(bufferId, priorityView == 1 ? 1 : 0);
        if (packed < 0)
        {
            return false;
        }
        const int view = static_cast<int>(
            (static_cast<unsigned>(packed) >> PositionViewShift) & 0x3U);
        const int index = packed & PositionIndexMask;
        if (view < 0 || view > 1 || index < 0)
        {
            return false;
        }
        if (!IsViewVisible(view))
        {
            // NPPM_ACTIVATEDOC em uma sub-view oculta pode revelar o divisor
            // central. Operacoes internas nunca devem mudar o layout do editor.
            return false;
        }
        return SendMessageW(_nppData._nppHandle, NPPM_ACTIVATEDOC, view, index) != FALSE;
    }

    void MirrorLinkManager::RestoreOpenDocuments(
        const std::array<UINT_PTR, 2>& viewBuffers,
        UINT_PTR currentBuffer,
        int currentView,
        HWND focus) const
    {
        for (int view = 0; view <= 1; ++view)
        {
            const UINT_PTR bufferId = viewBuffers[static_cast<std::size_t>(view)];
            if (bufferId != 0 && IsViewVisible(view) && GetActiveBufferInView(view) != bufferId)
            {
                ActivateBufferInView(bufferId, view);
            }
        }
        const UINT_PTR activeBuffer = static_cast<UINT_PTR>(SendMessageW(
            _nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
        if (currentBuffer != 0 && activeBuffer != currentBuffer)
        {
            ActivateBufferInView(currentBuffer, currentView);
        }
        if (focus != nullptr && IsWindow(focus) != FALSE)
        {
            SetFocus(focus);
        }
    }

    std::wstring MirrorLinkManager::GetPathFromBufferId(UINT_PTR bufferId) const
    {
        const int length = static_cast<int>(SendMessageW(
            _nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, bufferId, 0));
        if (length < 0)
        {
            return {};
        }
        std::wstring path(static_cast<std::size_t>(length) + 1U, L'\0');
        const int copied = static_cast<int>(SendMessageW(
            _nppData._nppHandle,
            NPPM_GETFULLPATHFROMBUFFERID,
            bufferId,
            reinterpret_cast<LPARAM>(path.data())));
        if (copied < 0)
        {
            return {};
        }
        path.resize(static_cast<std::size_t>(copied));
        return path;
    }

    bool MirrorLinkManager::GetDocumentUtf8(
        HWND scintilla,
        std::string& text,
        std::wstring& error) const
    {
        text.clear();
        error.clear();
        if (scintilla == nullptr || IsWindow(scintilla) == FALSE)
        {
            error = L"O controle Scintilla do documento nao esta disponivel.";
            return false;
        }

        const LRESULT length = SendMessageW(scintilla, SCI_GETLENGTH, 0, 0);
        if (length < 0 || length > MaxMirrorDocumentLength)
        {
            error = L"O espelhamento foi interrompido: o arquivo ultrapassa 512 MB.";
            return false;
        }
        if (length == 0)
        {
            return true;
        }

        const UINT internalCodePage = static_cast<UINT>(SendMessageW(
            scintilla, SCI_GETCODEPAGE, 0, 0));
        if (internalCodePage == CP_UTF8)
        {
            text.assign(static_cast<std::size_t>(length) + 1U, '\0');
            SendMessageW(
                scintilla,
                SCI_GETTEXT,
                static_cast<WPARAM>(text.size()),
                reinterpret_cast<LPARAM>(text.data()));
            text.resize(static_cast<std::size_t>(length));
            if (text.find('\0') != std::string::npos)
            {
                text.clear();
                error = L"O arquivo contem bytes NUL e nao pode ser espelhado com seguranca.";
                return false;
            }
            return true;
        }

        SendMessageW(scintilla, SCI_SETTARGETSTART, 0, 0);
        SendMessageW(scintilla, SCI_SETTARGETEND, static_cast<WPARAM>(length), 0);
        const LRESULT utf8Length = SendMessageW(scintilla, SCI_TARGETASUTF8, 0, 0);
        if (utf8Length < 0 || utf8Length > MaxMirrorDocumentLength * 3)
        {
            error = L"Nao foi possivel calcular o tamanho UTF-8 do documento.";
            return false;
        }
        if (utf8Length == 0 && length > 0)
        {
            error = L"O Scintilla nao conseguiu converter o documento aberto para UTF-8.";
            return false;
        }

        text.assign(static_cast<std::size_t>(utf8Length) + 1U, '\0');
        const LRESULT copied = SendMessageW(
            scintilla,
            SCI_TARGETASUTF8,
            0,
            reinterpret_cast<LPARAM>(text.data()));
        if (copied != utf8Length)
        {
            text.clear();
            error = L"O conteudo mudou durante a captura. Tente sincronizar novamente.";
            return false;
        }
        text.resize(static_cast<std::size_t>(copied));
        if (text.find('\0') != std::string::npos)
        {
            text.clear();
            error = L"O arquivo contem bytes NUL e nao pode ser espelhado com seguranca.";
            return false;
        }
        return true;
    }

    MirrorDocumentEncoding MirrorLinkManager::CaptureActiveDocumentEncoding(
        HWND scintilla,
        UINT_PTR bufferId) const
    {
        MirrorDocumentEncoding encoding{};
        encoding.unicodeMode = static_cast<int>(SendMessageW(
            _nppData._nppHandle, NPPM_GETBUFFERENCODING, bufferId, 0));
        encoding.internalCodePage = static_cast<UINT>(SendMessageW(
            scintilla, SCI_GETCODEPAGE, 0, 0));
        encoding.saveCodePage = encoding.unicodeMode == Uni8Bit
            ? GetCheckedCustomEncodingCodePage()
            : -1;
        if (encoding.unicodeMode == Uni8Bit &&
            encoding.saveCodePage <= 0 &&
            encoding.internalCodePage > 1 &&
            encoding.internalCodePage != CP_UTF8)
        {
            encoding.saveCodePage = static_cast<int>(encoding.internalCodePage);
        }
        return encoding;
    }

    int MirrorLinkManager::GetCheckedCustomEncodingCodePage() const
    {
        const HMENU mainMenu = reinterpret_cast<HMENU>(SendMessageW(
            _nppData._nppHandle, NPPM_GETMENUHANDLE, NPPMAINMENU, 0));
        if (mainMenu == nullptr)
        {
            return -1;
        }
        for (std::size_t index = 0; index < CustomEncodingCodePages.size(); ++index)
        {
            const int codePage = CustomEncodingCodePages[index];
            if (codePage <= 0)
            {
                continue;
            }
            const UINT state = GetMenuState(
                mainMenu,
                static_cast<UINT>(IdmFormatEncode + static_cast<int>(index)),
                MF_BYCOMMAND);
            if (state != static_cast<UINT>(-1) && (state & MF_CHECKED) != 0)
            {
                return codePage;
            }
        }
        return -1;
    }

    MirrorLinkManager::LinkState* MirrorLinkManager::FindLinkBySource(UINT_PTR bufferId)
    {
        const auto found = std::find_if(
            _links.begin(),
            _links.end(),
            [bufferId](const std::unique_ptr<LinkState>& link)
            {
                return link->sourceBufferId == bufferId;
            });
        return found == _links.end() ? nullptr : found->get();
    }

    const MirrorLinkManager::LinkState* MirrorLinkManager::FindLinkBySource(UINT_PTR bufferId) const
    {
        const auto found = std::find_if(
            _links.begin(),
            _links.end(),
            [bufferId](const std::unique_ptr<LinkState>& link)
            {
                return link->sourceBufferId == bufferId;
            });
        return found == _links.end() ? nullptr : found->get();
    }

    MirrorLinkManager::LinkState* MirrorLinkManager::FindLinkByMirror(UINT_PTR bufferId)
    {
        const auto found = std::find_if(
            _links.begin(),
            _links.end(),
            [bufferId](const std::unique_ptr<LinkState>& link)
            {
                return link->mirrorBufferId == bufferId;
            });
        return found == _links.end() ? nullptr : found->get();
    }

    const MirrorLinkManager::LinkState* MirrorLinkManager::FindLinkByMirror(UINT_PTR bufferId) const
    {
        const auto found = std::find_if(
            _links.begin(),
            _links.end(),
            [bufferId](const std::unique_ptr<LinkState>& link)
            {
                return link->mirrorBufferId == bufferId;
            });
        return found == _links.end() ? nullptr : found->get();
    }

    MirrorLinkManager::LinkState* MirrorLinkManager::FindLinkByPair(
        UINT_PTR sourceBufferId,
        UINT_PTR mirrorBufferId)
    {
        const auto found = std::find_if(
            _links.begin(),
            _links.end(),
            [sourceBufferId, mirrorBufferId](const std::unique_ptr<LinkState>& link)
            {
                return link->sourceBufferId == sourceBufferId &&
                    link->mirrorBufferId == mirrorBufferId;
            });
        return found == _links.end() ? nullptr : found->get();
    }

    const MirrorLinkManager::LinkState* MirrorLinkManager::FindLinkByPair(
        UINT_PTR sourceBufferId,
        UINT_PTR mirrorBufferId) const
    {
        const auto found = std::find_if(
            _links.begin(),
            _links.end(),
            [sourceBufferId, mirrorBufferId](const std::unique_ptr<LinkState>& link)
            {
                return link->sourceBufferId == sourceBufferId &&
                    link->mirrorBufferId == mirrorBufferId;
            });
        return found == _links.end() ? nullptr : found->get();
    }

    bool MirrorLinkManager::BufferParticipatesInLink(
        UINT_PTR bufferId,
        const LinkState* except) const
    {
        return std::any_of(
            _links.begin(),
            _links.end(),
            [bufferId, except](const std::unique_ptr<LinkState>& link)
            {
                return link.get() != except &&
                    (link->sourceBufferId == bufferId || link->mirrorBufferId == bufferId);
            });
    }

    void MirrorLinkManager::UpdateToolbarSummary(
        MirrorToolbarState preferredState,
        const std::wstring& detail)
    {
        if (_links.empty())
        {
            UpdateToolbar(
                MirrorToolbarState::Unlinked,
                detail.empty()
                    ? L"Sem vinculos. Clique em Vincular para selecionar os pares S_ e C_."
                    : detail);
            return;
        }

        std::wstring text = detail;
        if (text.empty())
        {
            text = std::to_wstring(_links.size()) +
                (_links.size() == 1U ? L" vinculo ativo." : L" vinculos ativos.");
            text += L"\r\nS_ envia para C_. Clique em Sync para sincronizar e salvar todos.";
        }
        UpdateToolbar(preferredState, text);
    }

    std::wstring MirrorLinkManager::NormalizePath(const std::wstring& path)
    {
        if (path.empty())
        {
            return {};
        }
        const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        std::wstring normalized;
        if (required > 0)
        {
            normalized.assign(static_cast<std::size_t>(required), L'\0');
            const DWORD copied = GetFullPathNameW(path.c_str(), required, normalized.data(), nullptr);
            if (copied > 0 && copied < required)
            {
                normalized.resize(copied);
            }
            else
            {
                normalized = path;
            }
        }
        else
        {
            normalized = path;
        }
        std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
        std::transform(
            normalized.begin(),
            normalized.end(),
            normalized.begin(),
            [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
        return normalized;
    }

    std::wstring MirrorLinkManager::BaseName(const std::wstring& path)
    {
        const std::size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? path : path.substr(slash + 1U);
    }

    std::wstring MirrorLinkManager::DirectoryName(const std::wstring& path)
    {
        const std::size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
    }

    bool MirrorLinkManager::IsServerName(const std::wstring& name)
    {
        return name.size() > 6U &&
            (name[0] == L'S' || name[0] == L's') &&
            name[1] == L'_' &&
            _wcsicmp(name.c_str() + name.size() - 4U, L".ini") == 0;
    }

    bool MirrorLinkManager::IsClientName(const std::wstring& name)
    {
        return name.size() > 6U &&
            (name[0] == L'C' || name[0] == L'c') &&
            name[1] == L'_' &&
            _wcsicmp(name.c_str() + name.size() - 4U, L".ini") == 0;
    }

    bool MirrorLinkManager::NamesMatch(
        const std::wstring& serverName,
        const std::wstring& clientName)
    {
        return IsServerName(serverName) && IsClientName(clientName) &&
            _wcsicmp(serverName.c_str() + 2, clientName.c_str() + 2) == 0;
    }

    std::wstring MirrorLinkManager::EncodingName(const MirrorDocumentEncoding& encoding)
    {
        std::wstring name;
        if (encoding.saveCodePage > 0)
        {
            name = L"Code page " + std::to_wstring(encoding.saveCodePage);
        }
        else
        {
            switch (encoding.unicodeMode)
            {
            case 0: name = L"ANSI/DBCS"; break;
            case 1: name = L"UTF-8 com BOM"; break;
            case 2: name = L"UTF-16 BE com BOM"; break;
            case 3: name = L"UTF-16 LE com BOM"; break;
            case 4: name = L"UTF-8 sem BOM"; break;
            case 5: name = L"7-bit/ASCII"; break;
            case 6: name = L"UTF-16 BE sem BOM"; break;
            case 7: name = L"UTF-16 LE sem BOM"; break;
            default: name = L"Desconhecido"; break;
            }
        }
        name += L" [interno CP " +
            std::to_wstring(ResolveInternalCodePage(encoding)) + L"]";
        return name;
    }

    bool MirrorLinkManager::IsEncodingCommand(UINT commandId)
    {
        return (commandId >= static_cast<UINT>(IdmFormatAnsi) &&
                commandId <= static_cast<UINT>(IdmFormatUtf16Le)) ||
            (commandId >= static_cast<UINT>(IdmFormatEncode) &&
             commandId <= static_cast<UINT>(IdmFormatEncodeLast));
    }

    void MirrorLinkManager::HandleEncodingCommandAfter(UINT commandId, UINT_PTR bufferId)
    {
        if (FindLinkBySource(bufferId) == nullptr && FindLinkByMirror(bufferId) == nullptr)
        {
            return;
        }
        const std::wstring reason =
            L"O encoding ou a code page de um arquivo vinculado foi alterado. O vinculo foi removido por seguranca.";
        Log(L"Comando de encoding " + std::to_wstring(commandId) + L" detectado. " + reason);
        UnlinkForBuffer(bufferId, reason, true, false);
    }

    LRESULT CALLBACK MirrorLinkManager::NotepadSubclassProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR referenceData)
    {
        auto* self = reinterpret_cast<MirrorLinkManager*>(referenceData);
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(window, NotepadSubclassProc, subclassId);
            if (self != nullptr)
            {
                self->_notepadSubclassInstalled = false;
            }
            return DefSubclassProc(window, message, wParam, lParam);
        }

        UINT commandId = 0;
        UINT_PTR linkedBufferId = 0;
        if (self != nullptr && message == WM_COMMAND)
        {
            commandId = LOWORD(wParam);
            if (IsEncodingCommand(commandId))
            {
                const UINT_PTR currentBufferId = static_cast<UINT_PTR>(SendMessageW(
                    self->_nppData._nppHandle,
                    NPPM_GETCURRENTBUFFERID,
                    0,
                    0));
                if (self->FindLinkBySource(currentBufferId) != nullptr ||
                    self->FindLinkByMirror(currentBufferId) != nullptr)
                {
                    linkedBufferId = currentBufferId;
                }
            }
        }

        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (self != nullptr && linkedBufferId != 0)
        {
            self->HandleEncodingCommandAfter(commandId, linkedBufferId);
        }
        return result;
    }

    void MirrorLinkManager::UpdateToolbar(MirrorToolbarState state, const std::wstring& detail)
    {
        if (_toolbar != nullptr)
        {
            _toolbar->SetState(state, detail);
        }
    }

    void MirrorLinkManager::ShowErrorOnce(const std::wstring& message)
    {
        if (_errorMessageShown)
        {
            return;
        }
        _errorMessageShown = true;
        MessageBoxW(
            _nppData._nppHandle,
            message.c_str(),
            L"NppGrandFantasia - Vinculos S_ / C_",
            MB_OK | MB_ICONERROR);
    }

    void MirrorLinkManager::Log(const std::wstring& message) const
    {
        const std::wstring path = LogFilePath();
        if (path.empty())
        {
            return;
        }
        const std::wstring line = L"[" + CurrentTimestamp() + L"] " + message + L"\r\n";
        const int required = WideCharToMultiByte(
            CP_UTF8,
            0,
            line.c_str(),
            static_cast<int>(line.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required <= 0)
        {
            return;
        }
        std::string utf8(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            line.c_str(),
            static_cast<int>(line.size()),
            utf8.data(),
            required,
            nullptr,
            nullptr);
        HANDLE file = CreateFileW(
            path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(file);
    }

    std::wstring MirrorLinkManager::LogFilePath() const
    {
        const int length = static_cast<int>(SendMessageW(
            _nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, 0, 0));
        if (length <= 0)
        {
            return {};
        }
        std::wstring directory(static_cast<std::size_t>(length) + 1U, L'\0');
        if (SendMessageW(
                _nppData._nppHandle,
                NPPM_GETPLUGINSCONFIGDIR,
                static_cast<WPARAM>(directory.size()),
                reinterpret_cast<LPARAM>(directory.data())) == FALSE)
        {
            return {};
        }
        directory.resize(static_cast<std::size_t>(length));
        if (!directory.empty() && directory.back() != L'\\')
        {
            directory.push_back(L'\\');
        }
        directory += L"NppGrandFantasia_mirror.log";
        return directory;
    }
}
