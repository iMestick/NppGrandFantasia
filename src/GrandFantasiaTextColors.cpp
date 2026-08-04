#include "GrandFantasiaTextColors.h"

#include <array>
#include <cctype>

namespace NppGrandFantasia
{
    namespace
    {
        constexpr PackedRgb PackRgb(unsigned int red, unsigned int green, unsigned int blue)
        {
            // Mesmo formato usado por COLORREF e pelas mensagens de cor do Scintilla.
            return (red & 0xFFU) | ((green & 0xFFU) << 8U) | ((blue & 0xFFU) << 16U);
        }

        constexpr std::array<PackedRgb, 73> TextColors{{
            0,
            PackRgb(60, 60, 60),       // $1$
            PackRgb(192, 192, 192),    // $2$
            PackRgb(169, 136, 113),    // $3$
            PackRgb(111, 93, 140),     // $4$
            PackRgb(102, 150, 221),    // $5$
            PackRgb(121, 153, 105),    // $6$
            PackRgb(251, 241, 119),    // $7$
            PackRgb(206, 91, 103),     // $8$
            PackRgb(228, 140, 126),    // $9$
            PackRgb(96, 67, 54),       // $10$
            PackRgb(255, 209, 197),    // $11$
            PackRgb(245, 122, 124),    // $12$
            PackRgb(154, 205, 247),    // $13$
            PackRgb(239, 139, 115),    // $14$
            PackRgb(187, 228, 119),    // $15$
            PackRgb(171, 154, 219),    // $16$
            PackRgb(255, 241, 228),    // $17$
            PackRgb(253, 228, 221),    // $18$
            PackRgb(214, 187, 84),     // $19$
            PackRgb(192, 156, 116),    // $20$
            PackRgb(156, 124, 105),    // $21$
            PackRgb(209, 100, 71),     // $22$
            PackRgb(128, 84, 70),      // $23$
            PackRgb(95, 95, 95),       // $24$
            PackRgb(71, 76, 110),      // $25$
            PackRgb(172, 172, 172),    // $26$
            PackRgb(192, 192, 192),    // $27$
            PackRgb(247, 220, 136),    // $28$
            PackRgb(224, 225, 119),    // $29$
            PackRgb(247, 232, 160),    // $30$
            PackRgb(251, 241, 119),    // $31$
            PackRgb(225, 119, 116),    // $32$
            PackRgb(100, 57, 54),      // $33$
            PackRgb(50, 92, 98),       // $34$
            PackRgb(176, 225, 114),    // $35$
            PackRgb(200, 227, 175),    // $36$
            PackRgb(171, 154, 219),    // $37$
            PackRgb(100, 82, 66),      // $38$
            PackRgb(141, 120, 95),     // $39$
            PackRgb(76, 73, 115),      // $40$
            PackRgb(154, 205, 247),    // $41$
            PackRgb(122, 134, 214),    // $42$
            PackRgb(178, 117, 111),    // $43$
            PackRgb(232, 172, 160),    // $44$
            PackRgb(62, 122, 125),     // $45$
            PackRgb(204, 179, 132),    // $46$
            PackRgb(225, 223, 222),    // $47$
            PackRgb(121, 153, 105),    // $48$
            PackRgb(171, 217, 176),    // $49$
            PackRgb(204, 218, 255),    // $50$
            PackRgb(195, 129, 111),    // $51$
            PackRgb(126, 183, 92),     // $52$
            PackRgb(179, 0, 124),      // $53$
            PackRgb(245, 211, 0),      // $54$
            PackRgb(191, 2, 34),       // $55$
            PackRgb(0, 177, 174),      // $56$
            PackRgb(233, 113, 23),     // $57$
            PackRgb(111, 85, 157),     // $58$
            PackRgb(72, 45, 230),      // $59$
            PackRgb(0, 120, 73),       // $60$
            PackRgb(84, 176, 228),     // $61$
            PackRgb(22, 96, 0),        // $62$
            PackRgb(254, 60, 88),      // $63$
            PackRgb(59, 130, 246),     // $64$
            PackRgb(204, 233, 255),    // $65$
            PackRgb(255, 245, 105),    // $66$
            PackRgb(190, 230, 100),    // $67$
            PackRgb(255, 164, 0),      // $68$
            PackRgb(245, 211, 0),      // $69$
            PackRgb(255, 255, 200),    // $70$
            PackRgb(154, 205, 247),    // $71$
            PackRgb(250, 125, 125),    // $72$
        }};
    }

    const std::array<PackedRgb, 73>& GrandFantasiaTextColorPalette()
    {
        return TextColors;
    }

    bool TryParseGrandFantasiaColorTag(
        std::string_view text,
        std::size_t position,
        GrandFantasiaColorTag& tag)
    {
        if (position >= text.size() || text[position] != '$')
        {
            return false;
        }

        std::size_t cursor = position + 1U;
        if (cursor >= text.size() ||
            std::isdigit(static_cast<unsigned char>(text[cursor])) == 0)
        {
            return false;
        }

        int value = 0;
        const std::size_t digitStart = cursor;
        while (cursor < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[cursor])) != 0)
        {
            value = value * 10 + (text[cursor] - '0');
            if (value > 72)
            {
                return false;
            }
            ++cursor;
        }

        if (cursor == digitStart || cursor >= text.size() || text[cursor] != '$' ||
            value < 1 || value > 72)
        {
            return false;
        }

        tag.position = position;
        tag.length = cursor - position + 1U;
        tag.value = value;
        tag.color = TextColors[static_cast<std::size_t>(value)];
        return true;
    }

    std::vector<GrandFantasiaColorSpan> BuildGrandFantasiaColorSpans(
        std::string_view recordText)
    {
        std::vector<GrandFantasiaColorSpan> spans;
        GrandFantasiaColorTag activeTag{};
        bool hasActiveColor = false;
        std::size_t activeStart = 0;

        for (std::size_t position = 0; position < recordText.size();)
        {
            GrandFantasiaColorTag nextTag{};
            if (!TryParseGrandFantasiaColorTag(recordText, position, nextTag))
            {
                ++position;
                continue;
            }

            if (hasActiveColor && position > activeStart)
            {
                spans.push_back({
                    activeStart,
                    position - activeStart,
                    activeTag.value,
                    activeTag.color,
                });
            }

            activeTag = nextTag;
            hasActiveColor = true;
            activeStart = position;
            position += nextTag.length;
        }

        if (hasActiveColor && activeStart < recordText.size())
        {
            spans.push_back({
                activeStart,
                recordText.size() - activeStart,
                activeTag.value,
                activeTag.color,
            });
        }

        return spans;
    }
}
