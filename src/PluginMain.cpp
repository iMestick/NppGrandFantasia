#include "ValidatorWindow.h"
#include "TranslationTabLabels.h"
#include "npp/PluginInterface.h"

#include <cwchar>
#include <memory>

namespace
{
    constexpr wchar_t PluginName[] = L"NppGrandFantasia";
    constexpr int CommandCount = 1;

    HINSTANCE g_instance = nullptr;
    NppData g_nppData{};
    FuncItem g_commands[CommandCount]{};
    ShortcutKey g_toggleShortcut{true, false, false, static_cast<UCHAR>('Q')};
    std::unique_ptr<NppGrandFantasia::ValidatorWindow> g_validatorWindow;
    std::unique_ptr<NppGrandFantasia::TranslationTabLabels> g_translationTabLabels;

    void TogglePipeValidator()
    {
        if (!g_validatorWindow)
        {
            g_validatorWindow = std::make_unique<NppGrandFantasia::ValidatorWindow>(
                g_instance,
                g_nppData,
                g_commands[0]._cmdID);
        }

        g_validatorWindow->Toggle();
    }

    void InitializeCommands()
    {
        wcsncpy_s(
            g_commands[0]._itemName,
            menuItemSize,
            L"Mostrar/Ocultar Validador de Pipes",
            _TRUNCATE);
        g_commands[0]._pFunc = TogglePipeValidator;
        g_commands[0]._init2Check = false;
        g_commands[0]._pShKey = &g_toggleShortcut;
    }
}

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    }

    return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData notepadPlusData)
{
    g_nppData = notepadPlusData;
    InitializeCommands();
}

extern "C" __declspec(dllexport) const wchar_t* getName()
{
    return PluginName;
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* nbF)
{
    if (nbF != nullptr)
    {
        *nbF = CommandCount;
    }

    return g_commands;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notifyCode)
{
    if (notifyCode == nullptr)
    {
        return;
    }

    switch (notifyCode->nmhdr.code)
    {
    case NPPN_READY:
        if (!g_translationTabLabels)
        {
            g_translationTabLabels = std::make_unique<NppGrandFantasia::TranslationTabLabels>(g_nppData);
        }

        if (!g_validatorWindow)
        {
            g_validatorWindow = std::make_unique<NppGrandFantasia::ValidatorWindow>(
                g_instance,
                g_nppData,
                g_commands[0]._cmdID);
            if (g_validatorWindow->Create())
            {
                g_validatorWindow->ScheduleValidation(true);
            }
        }
        if (g_translationTabLabels)
        {
            g_translationTabLabels->Refresh();
        }
        break;

    case NPPN_FILEBEFORESAVE:
        if (g_validatorWindow)
        {
            g_validatorWindow->HandleFileBeforeSave(
                static_cast<UINT_PTR>(notifyCode->nmhdr.idFrom));
        }
        break;

    case NPPN_FILESAVED:
        if (g_validatorWindow)
        {
            g_validatorWindow->HandleFileSaved(
                static_cast<UINT_PTR>(notifyCode->nmhdr.idFrom));
            g_validatorWindow->ScheduleValidation(true);
        }
        if (g_translationTabLabels)
        {
            g_translationTabLabels->Refresh();
        }
        break;

    case NPPN_FILEBEFORECLOSE:
        if (g_validatorWindow)
        {
            g_validatorWindow->HandleFileBeforeClose(
                static_cast<UINT_PTR>(notifyCode->nmhdr.idFrom));
        }
        break;

    case NPPN_FILECLOSED:
        if (g_validatorWindow)
        {
            g_validatorWindow->HandleFileClosed(
                static_cast<UINT_PTR>(notifyCode->nmhdr.idFrom));
            g_validatorWindow->ScheduleValidation(true);
        }
        if (g_translationTabLabels)
        {
            g_translationTabLabels->Refresh();
        }
        break;

    case NPPN_FILEBEFORERENAME:
    case NPPN_FILERENAMED:
        if (g_validatorWindow)
        {
            g_validatorWindow->HandleFilePathChanged(
                static_cast<UINT_PTR>(notifyCode->nmhdr.idFrom),
                L"Um dos arquivos vinculados foi renomeado. O vinculo foi removido.");
        }
        if (notifyCode->nmhdr.code == NPPN_FILERENAMED && g_translationTabLabels)
        {
            g_translationTabLabels->Refresh();
        }
        break;

    case NPPN_FILEBEFOREDELETE:
    case NPPN_FILEDELETED:
        if (g_validatorWindow)
        {
            g_validatorWindow->HandleFilePathChanged(
                static_cast<UINT_PTR>(notifyCode->nmhdr.idFrom),
                L"Um dos arquivos vinculados foi removido. O vinculo foi encerrado.");
        }
        if (notifyCode->nmhdr.code == NPPN_FILEDELETED && g_translationTabLabels)
        {
            g_translationTabLabels->Refresh();
        }
        break;

    case NPPN_READONLYCHANGED:
        if (g_validatorWindow)
        {
            g_validatorWindow->HandleReadOnlyChanged(
                reinterpret_cast<UINT_PTR>(notifyCode->nmhdr.hwndFrom));
        }
        break;

    case NPPN_BUFFERACTIVATED:
        if (g_validatorWindow)
        {
            g_validatorWindow->HandleBufferActivated();
            if (!g_validatorWindow->IsCapturingMirrorDocuments() &&
                !g_validatorWindow->IsApplyingMirrorUpdate())
            {
                g_validatorWindow->ScheduleValidation(true);
            }
        }
        if (g_translationTabLabels)
        {
            g_translationTabLabels->Refresh();
        }
        break;

    case NPPN_FILEOPENED:
        if (g_validatorWindow)
        {
            g_validatorWindow->ScheduleValidation(true);
        }
        if (g_translationTabLabels)
        {
            g_translationTabLabels->Refresh();
        }
        break;

    case NPPN_DOCORDERCHANGED:
        if (g_translationTabLabels)
        {
            g_translationTabLabels->Refresh();
        }
        break;

    case NPPN_LANGCHANGED:
        if (g_validatorWindow)
        {
            g_validatorWindow->HandleBufferActivated();
            g_validatorWindow->ScheduleValidation(true);
        }
        break;

    case NPPN_GLOBALMODIFIED:
        if (g_validatorWindow && !g_validatorWindow->IsApplyingMirrorUpdate())
        {
            const UINT_PTR bufferId = reinterpret_cast<UINT_PTR>(notifyCode->nmhdr.hwndFrom);
            g_validatorWindow->HandleGlobalModified(bufferId);
            g_validatorWindow->ScheduleValidation(false);
        }
        break;

    case NPPN_DARKMODECHANGED:
        if (g_validatorWindow)
        {
            g_validatorWindow->ApplyDarkMode(false);
        }
        break;

    case SCN_UPDATEUI:
        if (g_validatorWindow && !g_validatorWindow->IsCapturingMirrorDocuments())
        {
            if ((notifyCode->updated & (SC_UPDATE_V_SCROLL | SC_UPDATE_H_SCROLL)) != 0)
            {
                g_validatorWindow->RefreshVisiblePipeColors(false);
            }

            if ((notifyCode->updated & SC_UPDATE_SELECTION) != 0)
            {
                g_validatorWindow->UpdateCompactStatus();
            }
        }
        break;

    case SCN_MODIFYATTEMPTRO:
        if (g_validatorWindow)
        {
            const UINT_PTR currentBufferId = static_cast<UINT_PTR>(SendMessageW(
                g_nppData._nppHandle,
                NPPM_GETCURRENTBUFFERID,
                0,
                0));
            g_validatorWindow->HandleReadOnlyModifyAttempt(currentBufferId);
        }
        break;

    case SCN_MODIFIED:
        if (g_validatorWindow &&
            !g_validatorWindow->IsApplyingMirrorUpdate() &&
            (notifyCode->modificationType &
             (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT | SC_PERFORMED_UNDO | SC_PERFORMED_REDO)) != 0)
        {
            const UINT_PTR currentBufferId = static_cast<UINT_PTR>(SendMessageW(
                g_nppData._nppHandle,
                NPPM_GETCURRENTBUFFERID,
                0,
                0));
            g_validatorWindow->HandleScintillaModified(currentBufferId);
            g_validatorWindow->ScheduleValidation(false);
        }
        break;

    case NPPN_SHUTDOWN:
        g_translationTabLabels.reset();
        if (g_validatorWindow)
        {
            g_validatorWindow->Destroy();
            g_validatorWindow.reset();
        }
        break;
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM)
{
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode()
{
    return TRUE;
}
