#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace NppGrandFantasia
{
    using PackedRgb = std::uint32_t;

    struct GrandFantasiaColorTag
    {
        std::size_t position = 0;
        std::size_t length = 0;
        int value = 0;
        PackedRgb color = 0;
    };

    struct GrandFantasiaColorSpan
    {
        std::size_t start = 0;
        std::size_t length = 0;
        int value = 0;
        PackedRgb color = 0;
    };

    // Retorna a paleta indexada pelo valor escrito no texto. O indice 0 nao e usado.
    const std::array<PackedRgb, 73>& GrandFantasiaTextColorPalette();

    // Reconhece somente marcadores completos no formato $1$ ate $72$.
    bool TryParseGrandFantasiaColorTag(
        std::string_view text,
        std::size_t position,
        GrandFantasiaColorTag& tag);

    // Gera os trechos coloridos de um unico registro. A cor comeca no marcador,
    // atravessa quebras de linha e permanece ativa ate outro marcador valido.
    std::vector<GrandFantasiaColorSpan> BuildGrandFantasiaColorSpans(
        std::string_view recordText);
}
