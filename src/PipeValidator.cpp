#include "PipeValidator.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <limits>
#include <string_view>
#include <utility>

namespace NppGrandFantasia
{
    namespace
    {
        struct LineView
        {
            std::string_view text;
            std::size_t number = 0;
        };

        struct TranslationPipeRule
        {
            std::string_view fileName;
            std::size_t expectedPipes;
        };

        constexpr std::array<TranslationPipeRule, 42> TranslationPipeRules{{
            {"t_achievement.ini", 3},
            {"t_activity.ini", 6},
            {"t_battlefield.ini", 2},
            {"t_beaststower.ini", 2},
            {"t_class.ini", 13},
            {"t_collection.ini", 2},
            {"t_dialogue.ini", 2},
            {"t_dynamicevent.ini", 2},
            {"t_elf.ini", 3},
            {"t_elfcollect.ini", 3},
            {"t_elfking.ini", 3},
            {"t_elfracing.ini", 2},
            {"t_elftabletability.ini", 2},
            {"t_elftabletcombo.ini", 3},
            {"t_elfteamfight.ini", 2},
            {"t_elftemple.ini", 2},
            {"t_elftemplechallenge.ini", 2},
            {"t_elftrain.ini", 3},
            {"t_enchant.ini", 5},
            {"t_equipset.ini", 2},
            {"t_exam.ini", 4},
            {"t_familytree.ini", 2},
            {"t_festival.ini", 2},
            {"t_item.ini", 3},
            {"t_itemcombo.ini", 2},
            {"t_itemmall.ini", 3},
            {"t_mentorshipinstance.ini", 3},
            {"t_mission.ini", 3},
            {"t_monster.ini", 2},
            {"t_node.ini", 11},
            {"t_npc.ini", 2},
            {"t_pkpalacerank.ini", 2},
            {"t_pointability.ini", 2},
            {"t_race.ini", 2},
            {"t_racegroup.ini", 2},
            {"t_rainbowevent.ini", 3},
            {"t_recommendevents.ini", 4},
            {"t_ridecombo.ini", 2},
            {"t_spell.ini", 3},
            {"t_textindex.ini", 2},
            {"t_title.ini", 3},
            {"t_vip.ini", 2},
        }};

        std::string_view Trim(std::string_view value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
            {
                value.remove_prefix(1);
            }

            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
            {
                value.remove_suffix(1);
            }

            return value;
        }

        std::string_view RemoveUtf8Bom(std::string_view value)
        {
            constexpr unsigned char bom0 = 0xEF;
            constexpr unsigned char bom1 = 0xBB;
            constexpr unsigned char bom2 = 0xBF;

            if (value.size() >= 3 &&
                static_cast<unsigned char>(value[0]) == bom0 &&
                static_cast<unsigned char>(value[1]) == bom1 &&
                static_cast<unsigned char>(value[2]) == bom2)
            {
                value.remove_prefix(3);
            }

            return value;
        }

        std::string NormalizeFileName(std::string_view fileName)
        {
            const std::size_t separator = fileName.find_last_of("/\\");
            if (separator != std::string_view::npos)
            {
                fileName.remove_prefix(separator + 1);
            }

            std::string normalized;
            normalized.reserve(fileName.size());
            for (const char ch : fileName)
            {
                normalized.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch))));
            }
            return normalized;
        }

        bool IsValidVersion(std::string_view version)
        {
            if (version.empty() || version.front() == '.' || version.back() == '.')
            {
                return false;
            }

            bool previousWasDot = false;
            for (const char ch : version)
            {
                if (ch == '.')
                {
                    if (previousWasDot)
                    {
                        return false;
                    }
                    previousWasDot = true;
                    continue;
                }

                if (std::isdigit(static_cast<unsigned char>(ch)) == 0)
                {
                    return false;
                }

                previousWasDot = false;
            }

            return true;
        }

        bool TryParsePositiveSize(std::string_view value, std::size_t& output)
        {
            if (value.empty())
            {
                return false;
            }

            unsigned long long parsed = 0;
            const char* begin = value.data();
            const char* end = value.data() + value.size();
            const auto result = std::from_chars(begin, end, parsed);

            if (result.ec != std::errc{} || result.ptr != end || parsed == 0 ||
                parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
            {
                return false;
            }

            output = static_cast<std::size_t>(parsed);
            return true;
        }

        bool TryParseHeader(
            std::string_view rawLine,
            std::string& version,
            std::size_t& expectedPipes,
            bool& looksLikeHeader)
        {
            std::string_view line = Trim(RemoveUtf8Bom(rawLine));
            looksLikeHeader = line.rfind("|V.", 0) == 0;

            if (!looksLikeHeader || line.size() < 7 || line.back() != '|')
            {
                return false;
            }

            const std::size_t versionEnd = line.find('|', 3);
            if (versionEnd == std::string_view::npos)
            {
                return false;
            }

            const std::size_t countEnd = line.find('|', versionEnd + 1);
            if (countEnd == std::string_view::npos || countEnd != line.size() - 1)
            {
                return false;
            }

            const std::string_view versionView = line.substr(3, versionEnd - 3);
            const std::string_view countView = line.substr(versionEnd + 1, countEnd - versionEnd - 1);

            if (!IsValidVersion(versionView) || !TryParsePositiveSize(countView, expectedPipes))
            {
                return false;
            }

            version.assign(versionView.begin(), versionView.end());
            return true;
        }

        bool TryGetRecordId(std::string_view line, long long& id)
        {
            line = RemoveUtf8Bom(line);
            if (line.empty() || std::isdigit(static_cast<unsigned char>(line.front())) == 0)
            {
                return false;
            }

            std::size_t pipePosition = 0;
            while (pipePosition < line.size() &&
                   std::isdigit(static_cast<unsigned char>(line[pipePosition])) != 0)
            {
                ++pipePosition;
            }

            if (pipePosition == 0 || pipePosition >= line.size() || line[pipePosition] != '|')
            {
                return false;
            }

            const std::string_view idView = line.substr(0, pipePosition);
            const char* begin = idView.data();
            const char* end = idView.data() + idView.size();
            const auto parsed = std::from_chars(begin, end, id);
            return parsed.ec == std::errc{} && parsed.ptr == end;
        }

        std::vector<LineView> SplitLines(const std::string& text)
        {
            std::vector<LineView> lines;
            std::size_t start = 0;
            std::size_t lineNumber = 1;

            while (start < text.size())
            {
                const std::size_t newline = text.find('\n', start);
                const std::size_t end = newline == std::string::npos ? text.size() : newline;
                std::size_t visibleEnd = end;

                if (visibleEnd > start && text[visibleEnd - 1] == '\r')
                {
                    --visibleEnd;
                }

                lines.push_back({std::string_view(text.data() + start, visibleEnd - start), lineNumber});

                if (newline == std::string::npos)
                {
                    break;
                }

                start = newline + 1;
                ++lineNumber;
            }

            if (text.empty())
            {
                lines.push_back({std::string_view{}, 1});
            }

            return lines;
        }

        std::size_t CountPipes(std::string_view value)
        {
            return static_cast<std::size_t>(std::count(value.begin(), value.end(), '|'));
        }

        void AnalyzeRecords(
            const std::vector<LineView>& lines,
            std::size_t startIndex,
            PipeValidationResult& result)
        {
            bool hasCurrentRecord = false;
            long long currentId = 0;
            std::size_t currentStartLine = 0;
            std::size_t currentEndLine = 0;
            std::size_t currentPipeCount = 0;
            bool firstErrorSeen = false;

            auto finishRecord = [&]()
            {
                if (!hasCurrentRecord)
                {
                    return;
                }

                ++result.recordCount;

                const PipeRecordInfo record{
                    currentId,
                    currentStartLine,
                    currentEndLine,
                    result.expectedPipes,
                    currentPipeCount,
                };
                result.records.push_back(record);

                if (record.IsValid())
                {
                    ++result.validRecordCount;
                    if (!firstErrorSeen)
                    {
                        result.hasLastValidIdBeforeFirstError = true;
                        result.lastValidIdBeforeFirstError = currentId;
                    }
                }
                else
                {
                    firstErrorSeen = true;
                    result.errors.push_back(record);
                }
            };

            for (std::size_t index = startIndex; index < lines.size(); ++index)
            {
                const LineView& line = lines[index];
                long long recordId = 0;

                if (TryGetRecordId(line.text, recordId))
                {
                    finishRecord();
                    hasCurrentRecord = true;
                    currentId = recordId;
                    currentStartLine = line.number;
                    currentEndLine = line.number;
                    currentPipeCount = CountPipes(line.text);
                }
                else if (hasCurrentRecord)
                {
                    currentEndLine = line.number;
                    currentPipeCount += CountPipes(line.text);
                }
            }

            finishRecord();
        }
    }

    bool TryGetTranslationPipeCount(
        std::string_view fileName,
        std::size_t& expectedPipes,
        std::string* normalizedFileName)
    {
        const std::string normalized = NormalizeFileName(fileName);
        if (normalizedFileName != nullptr)
        {
            *normalizedFileName = normalized;
        }

        for (const TranslationPipeRule& rule : TranslationPipeRules)
        {
            if (normalized == rule.fileName)
            {
                expectedPipes = rule.expectedPipes;
                return true;
            }
        }

        return false;
    }

    PipeValidationResult AnalyzeIniText(const std::string& text, std::string_view fileName)
    {
        PipeValidationResult result;
        const std::vector<LineView> lines = SplitLines(text);

        std::size_t firstContentIndex = lines.size();
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            if (!Trim(RemoveUtf8Bom(lines[index].text)).empty())
            {
                firstContentIndex = index;
                break;
            }
        }

        if (firstContentIndex != lines.size())
        {
            bool looksLikeHeader = false;
            result.headerLine = lines[firstContentIndex].number;
            result.headerValid = TryParseHeader(
                lines[firstContentIndex].text,
                result.version,
                result.expectedPipes,
                looksLikeHeader);
            result.headerPresent = looksLikeHeader;
        }

        std::size_t translationPipes = 0;
        std::string normalizedTranslationFile;
        if (TryGetTranslationPipeCount(fileName, translationPipes, &normalizedTranslationFile))
        {
            result.mode = PipeValidationMode::TranslationFile;
            result.validationActive = true;
            result.translationRuleMatched = true;
            result.translationFileName = std::move(normalizedTranslationFile);
            result.expectedPipes = translationPipes;

            // Os INIs de traducao usam a regra fixa do nome do arquivo e nao
            // dependem de cabecalho. Qualquer texto antes do primeiro ID| e ignorado.
            AnalyzeRecords(lines, 0, result);
            return result;
        }

        if (!result.headerValid)
        {
            return result;
        }

        result.mode = PipeValidationMode::Header;
        result.validationActive = true;
        AnalyzeRecords(lines, firstContentIndex + 1, result);
        return result;
    }
}
