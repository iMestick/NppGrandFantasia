#include "PipeValidator.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

using NppGrandFantasia::AnalyzeIniText;
using NppGrandFantasia::PipeValidationMode;
using NppGrandFantasia::TryGetTranslationPipeCount;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FALHA: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    {
        const auto result = AnalyzeIniText("|V.5|4|\n100|A|B|C|\n101|D|E|F|\n");
        Require(result.validationActive, "validacao por cabecalho deveria estar ativa");
        Require(result.mode == PipeValidationMode::Header, "modo por cabecalho incorreto");
        Require(result.headerValid, "cabecalho deveria ser valido");
        Require(result.expectedPipes == 4, "quantidade esperada incorreta");
        Require(result.recordCount == 2, "deveria encontrar dois registros");
        Require(result.records.size() == 2, "deveria retornar os dados dos registros");
        Require(result.records[0].id == 100, "ID do primeiro registro incorreto");
        Require(result.records[0].startLine == 2 && result.records[0].endLine == 2,
                "intervalo do primeiro registro incorreto");
        Require(result.records[0].actualPipes == 4 && result.records[0].IsValid(),
                "resumo do primeiro registro incorreto");
        Require(result.errors.empty(), "nao deveria ter erros");
    }

    {
        const auto result = AnalyzeIniText("|V.5|4|\n100|A|B|\n101|D|E|F|G|\n");
        Require(result.errors.size() == 2, "deveria listar pipes faltando e sobrando");
        Require(result.errors[0].startLine == 2, "linha do primeiro erro incorreta");
        Require(result.errors[0].actualPipes == 3, "contagem do primeiro erro incorreta");
        Require(result.errors[1].actualPipes == 5, "contagem do segundo erro incorreta");
    }

    {
        const auto result = AnalyzeIniText("\n\xEF\xBB\xBF|V.16|5|\n200|A|\nB|C|D|\n");
        Require(result.headerValid, "BOM e linha vazia antes do cabecalho deveriam ser aceitos");
        Require(result.recordCount == 1, "registro multilinha nao encontrado");
        Require(result.errors.empty(), "registro multilinha deveria ser valido");
    }

    {
        const auto result = AnalyzeIniText("100|A|B|C|\n");
        Require(!result.validationActive, "arquivo desconhecido sem cabecalho deve ser ignorado");
        Require(!result.headerValid, "arquivo sem cabecalho deve ser ignorado");
        Require(result.recordCount == 0, "arquivo ignorado nao deve ser analisado");
    }

    {
        const auto result = AnalyzeIniText("|V.x|4|\n100|A|B|C|\n");
        Require(result.headerPresent, "cabecalho invalido deveria ser reconhecido como presente");
        Require(!result.headerValid, "versao nao numerica deve invalidar o cabecalho");
        Require(!result.validationActive, "cabecalho invalido nao deve ativar a validacao");
    }

    {
        const auto result = AnalyzeIniText("|V.2|4|\n10|A|B|C|\n11|A|B|\n12|A|B|C|\n");
        Require(result.errors.size() == 1, "deveria manter apenas o registro quebrado");
        Require(result.errors[0].startLine == 3 && result.errors[0].endLine == 3,
                "intervalo de linha do erro incorreto");
        Require(result.hasLastValidIdBeforeFirstError, "deveria guardar o ultimo ID valido");
        Require(result.lastValidIdBeforeFirstError == 10, "ultimo ID valido incorreto");
    }

    {
        const auto result = AnalyzeIniText("|V.2|5|\n20|A|\nB|C|D|\n");
        Require(result.errors.empty(), "registro multilinha final deveria ser valido");
        Require(result.recordCount == 1, "registro multilinha final contado incorretamente");
    }

    {
        const auto result = AnalyzeIniText(
            "100|Nome|Descricao|\n101|Outro|Texto|\n",
            "t_item.ini");
        Require(result.validationActive, "t_item.ini deveria ativar a validacao sem cabecalho");
        Require(result.mode == PipeValidationMode::TranslationFile,
                "t_item.ini deveria usar modo de traducao");
        Require(result.translationRuleMatched, "regra de traducao deveria ser encontrada");
        Require(result.expectedPipes == 3, "t_item.ini deveria exigir 3 pipes");
        Require(result.recordCount == 2, "t_item.ini deveria encontrar dois registros");
        Require(result.errors.empty(), "t_item.ini valido nao deveria apresentar erros");
    }

    {
        const auto result = AnalyzeIniText(
            "100|Nome|\n101|Outro|Texto|CampoExtra|\n",
            "C:\\GrandFantasia\\Data\\DB\\T_ITEM.INI");
        Require(result.validationActive, "caminho e caixa alta deveriam ser aceitos");
        Require(result.translationFileName == "t_item.ini", "nome normalizado incorreto");
        Require(result.errors.size() == 2,
                "t_item.ini deveria detectar um pipe faltando e um sobrando");
        Require(result.errors[0].actualPipes == 2, "erro de pipe faltando incorreto");
        Require(result.errors[1].actualPipes == 4, "erro de pipe sobrando incorreto");
    }

    {
        const auto result = AnalyzeIniText(
            "|V.999|99|\n100|Nome|Descricao|\n",
            "t_item.ini");
        Require(result.mode == PipeValidationMode::TranslationFile,
                "regra fixa de traducao deve ter prioridade sobre o cabecalho");
        Require(result.expectedPipes == 3,
                "cabecalho nao deve substituir a regra fixa do t_item.ini");
        Require(result.errors.empty(), "registro de traducao deveria continuar valido");
    }

    {
        const auto result = AnalyzeIniText(
            "\xEF\xBB\xBF" "100|Texto|\n",
            "t_monster.ini");
        Require(result.expectedPipes == 2, "t_monster.ini deveria exigir 2 pipes");
        Require(result.errors.empty(), "BOM no primeiro ID de traducao deveria ser aceito");
    }

    {
        struct ExpectedRule
        {
            const char* fileName;
            std::size_t pipeCount;
        };

        constexpr std::array<ExpectedRule, 42> rules{{
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

        for (const ExpectedRule& rule : rules)
        {
            std::size_t actual = 0;
            Require(TryGetTranslationPipeCount(rule.fileName, actual),
                    "regra de arquivo de traducao ausente");
            Require(actual == rule.pipeCount, "quantidade de pipes da regra incorreta");
        }

        std::size_t ignored = 0;
        Require(!TryGetTranslationPipeCount("t_unknown.ini", ignored),
                "arquivo de traducao desconhecido nao deveria ter regra");
    }

    std::cout << "Todos os testes passaram.\n";
    return EXIT_SUCCESS;
}
