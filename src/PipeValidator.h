#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace NppGrandFantasia
{
    enum class PipeValidationMode
    {
        None,
        Header,
        TranslationFile,
    };

    struct PipeRecordInfo
    {
        long long id = 0;
        std::size_t startLine = 0;
        std::size_t endLine = 0;
        std::size_t expectedPipes = 0;
        std::size_t actualPipes = 0;

        bool IsValid() const
        {
            return actualPipes == expectedPipes;
        }
    };

    using PipeRecordError = PipeRecordInfo;

    struct PipeValidationResult
    {
        PipeValidationMode mode = PipeValidationMode::None;
        bool validationActive = false;

        bool headerPresent = false;
        bool headerValid = false;
        std::size_t headerLine = 0;
        std::string version;

        bool translationRuleMatched = false;
        std::string translationFileName;

        std::size_t expectedPipes = 0;
        std::size_t recordCount = 0;
        std::size_t validRecordCount = 0;
        bool hasLastValidIdBeforeFirstError = false;
        long long lastValidIdBeforeFirstError = 0;
        std::vector<PipeRecordInfo> records;
        std::vector<PipeRecordError> errors;
    };

    // fileName pode ser apenas o nome (t_item.ini) ou um caminho completo.
    // A comparacao dos INIs de traducao nao diferencia maiusculas de minusculas.
    PipeValidationResult AnalyzeIniText(
        const std::string& text,
        std::string_view fileName = {});

    bool TryGetTranslationPipeCount(
        std::string_view fileName,
        std::size_t& expectedPipes,
        std::string* normalizedFileName = nullptr);
}
