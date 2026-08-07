#include "FlagToolWindow.h"

#include <algorithm>
#include <array>
#include <commctrl.h>
#include <cwctype>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>

namespace NppGrandFantasia
{
    namespace
    {
        constexpr wchar_t FlagToolWindowClass[] = L"NppGrandFantasia.FlagTool";

        constexpr int IdTabSelector = 100;
        constexpr int IdCheck = 101;
        constexpr int IdCopy = 102;
        constexpr int IdMarkAll = 103;
        constexpr int IdClearAll = 104;
        constexpr int IdFlagBase = 1000;
        constexpr int IdTabMenuBase = 4000;

        constexpr int WindowWidth = 1280;
        constexpr int WindowHeight = 720;
        constexpr int ToolbarHeight = 50;
        constexpr int StatusbarHeight = 52;
        constexpr int CellWidth = 150;
        constexpr int CellHeight = 44;
        constexpr int CellPadding = 4;

        constexpr COLORREF ColorBackground = RGB(13, 17, 23);       // #0D1117
        constexpr COLORREF ColorHeader = RGB(22, 27, 34);           // #161B22
        constexpr COLORREF ColorCard = RGB(17, 24, 32);             // #111820
        constexpr COLORREF ColorHighlight = RGB(47, 188, 114);      // #2FBC72
        constexpr COLORREF ColorText = RGB(240, 246, 252);          // #F0F6FC
        constexpr COLORREF ColorButton = RGB(33, 38, 45);           // #21262D
        constexpr COLORREF ColorButtonText = RGB(240, 246, 252);    // #F0F6FC
        constexpr COLORREF ColorButtonActiveText = RGB(7, 19, 12);  // #07130C
        constexpr COLORREF ColorFrameSelected = RGB(47, 188, 114); // #2FBC72
        constexpr COLORREF ColorFrameNormal = RGB(21, 27, 35);      // #151B23
        constexpr COLORREF ColorFrameText = RGB(7, 19, 12);         // #07130C
        constexpr COLORREF ColorFrameNull = RGB(11, 15, 20);        // #0B0F14
        constexpr COLORREF ColorBorder = RGB(48, 54, 61);           // #30363D

        struct FlagSpec
        {
            const wchar_t* name;
            const wchar_t* value;
        };

        constexpr FlagSpec ItemFlags[] =
        {
            {L"Can Use", L"1"},
            {L"No Decrease", L"2"},
            {L"No Trade", L"4"},
            {L"No Discard", L"8"},
            {L"No Enhance", L"16"},
            {L"No Repair", L"32"},
            {L"Combinable", L"64"},
            {L"Bind On Equip", L"128"},
            {L"Accum Time", L"256"},
            {L"No Same Buff", L"512"},
            {L"No In Battle", L"1024"},
            {L"No In Town", L"2048"},
            {L"No In Cave", L"4096"},
            {L"No In Instance", L"8192"},
            {L"Link To Quest", L"16384"},
            {L"For Dead", L"32768"},
            {L"Only 1", L"65536"},
            {L"Only 2", L"131072"},
            {L"Only 3", L"262144"},
            {L"Only 4", L"524288"},
            {L"Only 5", L"1048576"},
            {L"Replaceable 1", L"2097152"},
            {L"Replaceable 2", L"4194304"},
            {L"Replaceable 3", L"8388608"},
            {L"Replaceable 4", L"16777216"},
            {L"Replaceable 5", L"33554432"},
            {L"No In Battlefield", L"67108864"},
            {L"No In Field", L"134217728"},
            {L"No Transfer Node", L"268435456"},
            {L"UnBind Item", L"536870912"},
            {L"Only Equip", L"2147483648"},
        };

        constexpr FlagSpec ItemPlusFlags[] =
        {
            {L"IK Combine", L"1"},
            {L"GK Combine", L"2"},
            {L"Equip Show", L"4"},
            {L"Purple W Limit", L"8"},
            {L"Purple A Limit", L"16"},
            {L"Use Bind", L"32"},
            {L"One Stack", L"64"},
            {L"Ride Combine IK", L"128"},
            {L"Ride Combine GK", L"256"},
            {L"VIP", L"512"},
            {L"Chair Combine IK", L"2048"},
            {L"Chair Combine GK", L"4096"},
            {L"Red W Limit", L"8192"},
            {L"Red A Limit", L"16384"},
            {L"Crystal Combo", L"32768"},
            {L"Souvenir Combo", L"65536"},
            {L"God Area Combo", L"131072"},
            {L"Elf Tablet Equip", L"262144"},
            {L"Elf Tablet Exp", L"524288"},
            {L"Show Probability", L"1048576"},
            {L"Storage Forbidden", L"2097152"},
            {L"Family Storage Forbidden", L"4194304"},
        };

        constexpr FlagSpec EnchantFlags[] =
        {
            {L"Remove Attack", L"1"},
            {L"Remove Be Attacked", L"2"},
            {L"Remove Hurt", L"4"},
            {L"Enchant Reserve", L"8"},
            {L"Can t Abandon", L"16"},
            {L"Die Reserve", L"32"},
            {L"Vulture Only", L"64"},
            {L"Wolf Only", L"128"},
            {L"Gorilla Only", L"256"},
            {L"No Boss", L"512"},
            {L"No Transfer Node", L"1024"},
            {L"Daily Type", L"2048"},
            {L"Machine Only", L"4096"},
            {L"No Use Item", L"8192"},
            {L"No Totem", L"16384"},
            {L"Hide Icon", L"32768"},
            {L"VIP", L"65536"},
            {L"No Move", L"131072"},
            {L"Heal", L"262144"},
            {L"No Rebirth", L"524288"},
            {L"No Battlefield", L"1048576"},
            {L"No Warning Msg", L"2097152"},
            {L"No Stack", L"4194304"},
            {L"Recover Priest Form", L"8388608"},
            {L"No Refresh", L"16777216"},
            {L"No Score", L"33554432"},
        };

        constexpr FlagSpec SpellOpFlagFlags[] =
        {
            {L"No In Battle", L"1"},
            {L"No In Town", L"2"},
            {L"No In Cave", L"4"},
            {L"No In Instance", L"8"},
            {L"No Boss", L"32"},
            {L"Only 0", L"64"},
            {L"Only 1", L"128"},
            {L"Only 2", L"256"},
            {L"Only 3", L"512"},
            {L"Only 4", L"1024"},
            {L"Replaceable 0", L"2048"},
            {L"Replaceable 1", L"4096"},
            {L"Replaceable 2", L"8192"},
            {L"Replaceable 3", L"16384"},
            {L"Replaceable 4", L"32768"},
            {L"For Dead", L"65536"},
            {L"No Hate", L"131072"},
            {L"Pet Only", L"262144"},
            {L"Unknown", L"524288"},
            {L"No In Battlefield", L"1048576"},
            {L"No In Field", L"2097152"},
            {L"Only Lover", L"4194304"},
            {L"No Totem", L"16777216"},
            {L"Unknown", L"33554432"},
            {L"Effect", L"67108864"},
            {L"Unknown", L"134217728"},
            {L"Link Buff Once", L"268435456"},
            {L"Link Buff Instant", L"536870912"},
            {L"Fix Cast Speed", L"1073741824"},
            {L"Link Move Mode", L"2147483648"},
            {L"Fix Cool Down", L"8589934592"},
            {L"Random Effect", L"17179869184"},
        };

        constexpr FlagSpec SpellRestrictEquipFlags[] =
        {
            {L"No restriction", L"0"},
            {L"Sword", L"64"},
            {L"Claymore", L"128"},
            {L"Mace", L"256"},
            {L"WarHammer", L"512"},
            {L"Axe", L"1024"},
            {L"BattleAxe", L"2048"},
            {L"Bow", L"4096"},
            {L"Gun", L"8192"},
            {L"HolyItem", L"16384"},
            {L"Staff", L"32768"},
            {L"Shield", L"65536"},
            {L"Machine", L"262144"},
            {L"HeavyMachine", L"524288"},
            {L"Cannon", L"1048576"},
            {L"CrystalKatana", L"2097152"},
            {L"CrystalKey", L"4194304"},
        };

        constexpr FlagSpec MissionFlags[] =
        {
            {L"No Abandon", L"1"},
            {L"No Share", L"2"},
            {L"Reaccept", L"4"},
            {L"Auto Accept", L"8"},
            {L"No Rebirth Reset", L"16"},
            {L"Daily Reset", L"32"},
            {L"Weekly Reset", L"64"},
            {L"Monthly Reset", L"128"},
        };

        constexpr FlagSpec MonsterSpecialFlagFlags[] =
        {
            {L"None", L"0"},
            {L"No Attack", L"1"},
            {L"No Move", L"2"},
            {L"Attack Limit", L"4"},
            {L"NoHit Back", L"8"},
            {L"N oPull", L"16"},
            {L"Combo Fix", L"32"},
            {L"Show Skill", L"128"},
            {L"Dynamic BlockIgnore Player", L"256"},
            {L"Dynamic BlockIgnore Monster", L"512"},
            {L"Dynamic BlockIgnore Skill", L"2048"},
            {L"Hide Target Name", L"8192"},
        };

        constexpr FlagSpec ClassNoviceFlags[] =
        {
            {L"Novice", L"0001"},
        };

        constexpr FlagSpec ClassFighterFlags[] =
        {
            {L"Fighter", L"0002"},
            {L"Warrior", L"0004"},
            {L"Berserker", L"0008"},
            {L"Paladin", L"0010"},
            {L"Titan", L"20000"},
            {L"Templar", L"40000"},
            {L"Death Knight", L"100000000"},
            {L"Royal Knight", L"200000000"},
            {L"Destroyer", L"10000000000"},
            {L"Sacred Knight", L"20000000000"},
        };

        constexpr FlagSpec ClassHunterFlags[] =
        {
            {L"Hunter", L"0020"},
            {L"Archer", L"0040"},
            {L"Ranger", L"0080"},
            {L"Assassin", L"0100"},
            {L"Sniper", L"80000"},
            {L"Shadow Sicarius", L"100000"},
            {L"Mercenary", L"400000000"},
            {L"Ninja", L"800000000"},
            {L"Predator", L"40000000000"},
            {L"Shinobi", L"80000000000"},
        };

        constexpr FlagSpec ClassAcolyteFlags[] =
        {
            {L"Acolyte", L"0200"},
            {L"Priest", L"0400"},
            {L"Cleric", L"0800"},
            {L"Sage", L"1000"},
            {L"Prophet", L"200000"},
            {L"Mystic", L"400000"},
            {L"Divine Master", L"1000000000"},
            {L"Shaman", L"2000000000"},
            {L"Archangel", L"100000000000"},
            {L"Druid", L"200000000000"},
        };

        constexpr FlagSpec ClassWarlockFlags[] =
        {
            {L"Warlock", L"2000"},
            {L"Magician", L"4000"},
            {L"Sorcerer", L"8000"},
            {L"Necromancer", L"10000"},
            {L"Archmage", L"800000"},
            {L"Demonologist", L"1000000"},
            {L"Arcane", L"4000000000"},
            {L"Lord of the Dead", L"8000000000"},
            {L"Warlock", L"400000000000"},
            {L"Shinigami", L"800000000000"},
        };

        constexpr FlagSpec ClassMachinistFlags[] =
        {
            {L"Apprentice", L"2000000"},
            {L"Machinist", L"4000000"},
            {L"Aggressor", L"8000000"},
            {L"Demolisher", L"10000000"},
            {L"Prime", L"20000000"},
            {L"Optimus", L"40000000"},
            {L"Megatron", L"1000000000000"},
            {L"Galvatron", L"2000000000000"},
            {L"Omega", L"4000000000000"},
            {L"Celestial Titan", L"8000000000000"},
        };

        constexpr FlagSpec ClassTravelerFlags[] =
        {
            {L"Traveler", L"10000000000000"},
            {L"Nomad", L"20000000000000"},
            {L"Swordsman", L"40000000000000"},
            {L"Illusionist", L"80000000000000"},
            {L"Samurai", L"100000000000000"},
            {L"Augur", L"200000000000000"},
            {L"Ronin", L"400000000000000"},
            {L"Oracle", L"800000000000000"},
            {L"Dimensional Master", L"1000000000000000"},
            {L"Chronos", L"2000000000000000"},
        };

        constexpr FlagSpec ClassMongeFlags[] =
        {
            {L"Apprentice of the path", L"4000000000000000"},
            {L"Aura Adept", L"8000000000000000"},
            {L"Warrior ascetic", L"10000000000000000"},
            {L"Martial Arts Avatar", L"20000000000000000"},
            {L"Monk", L"40000000000000000"},
            {L"Avatar", L"80000000000000000"},
        };

        struct SpecView
        {
            const FlagSpec* data = nullptr;
            std::size_t size = 0;
        };

        template <std::size_t N>
        constexpr SpecView View(const FlagSpec (&items)[N])
        {
            return {items, N};
        }

        constexpr std::array<SpecView, 8> ClassColumns =
        {
            View(ClassNoviceFlags),
            View(ClassFighterFlags),
            View(ClassHunterFlags),
            View(ClassAcolyteFlags),
            View(ClassWarlockFlags),
            View(ClassMachinistFlags),
            View(ClassTravelerFlags),
            View(ClassMongeFlags),
        };

        constexpr std::array<const wchar_t*, 8> TabNames =
        {
            L"Item",
            L"ItemPlus",
            L"Class",
            L"Enchant",
            L"Spell OpFlag",
            L"Spell RestrictEquip",
            L"Mission",
            L"Monster SpecialFlag",
        };

        HFONT CreateSegoeUiFont(HWND referenceWindow, int pointSize, bool bold)
        {
            UINT dpi = 96;
            using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
            const HMODULE user32 = GetModuleHandleW(L"user32.dll");
            const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFunction>(
                user32 != nullptr ? GetProcAddress(user32, "GetDpiForWindow") : nullptr);
            if (getDpiForWindow != nullptr && referenceWindow != nullptr)
            {
                dpi = getDpiForWindow(referenceWindow);
            }

            const int height = -MulDiv(pointSize, static_cast<int>(dpi), 72);
            return CreateFontW(
                height,
                0,
                0,
                0,
                bold ? FW_BOLD : FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI");
        }

        void FillSolidRect(HDC dc, const RECT& rect, COLORREF color)
        {
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
        }

        void FrameSolidRect(HDC dc, const RECT& rect, COLORREF color)
        {
            HBRUSH brush = CreateSolidBrush(color);
            FrameRect(dc, &rect, brush);
            DeleteObject(brush);
        }

        std::wstring FormatDisplayText(const std::wstring& text)
        {
            std::wstring result;
            result.reserve(text.size() + 8U);
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                wchar_t current = text[index] == L'_' ? L' ' : text[index];
                if (index > 0U && current != L' ')
                {
                    wchar_t previous = text[index - 1U] == L'_' ? L' ' : text[index - 1U];
                    const wchar_t next = index + 1U < text.size()
                        ? (text[index + 1U] == L'_' ? L' ' : text[index + 1U])
                        : L'\0';
                    const bool lowerOrDigitBeforeUpper =
                        (std::iswlower(previous) != 0 || std::iswdigit(previous) != 0) &&
                        std::iswupper(current) != 0;
                    const bool acronymBoundary =
                        std::iswupper(previous) != 0 &&
                        std::iswupper(current) != 0 &&
                        next != L'\0' &&
                        std::iswlower(next) != 0;
                    if (previous != L' ' && (lowerOrDigitBeforeUpper || acronymBoundary))
                    {
                        result.push_back(L' ');
                    }
                }
                result.push_back(current);
            }
            return result;
        }
    }

    bool FlagToolWindow::BigUInt::IsZero() const
    {
        for (const std::uint32_t limb : limbs)
        {
            if (limb != 0U)
            {
                return false;
            }
        }
        return true;
    }

    void FlagToolWindow::BigUInt::Normalize()
    {
        while (!limbs.empty() && limbs.back() == 0U)
        {
            limbs.pop_back();
        }
    }

    void FlagToolWindow::BigUInt::Clear()
    {
        limbs.clear();
    }

    void FlagToolWindow::BigUInt::OrWith(const BigUInt& other)
    {
        if (limbs.size() < other.limbs.size())
        {
            limbs.resize(other.limbs.size(), 0U);
        }
        for (std::size_t index = 0; index < other.limbs.size(); ++index)
        {
            limbs[index] |= other.limbs[index];
        }
        Normalize();
    }

    bool FlagToolWindow::BigUInt::ContainsBits(const BigUInt& other) const
    {
        for (std::size_t index = 0; index < other.limbs.size(); ++index)
        {
            const std::uint32_t own = index < limbs.size() ? limbs[index] : 0U;
            if ((own & other.limbs[index]) != other.limbs[index])
            {
                return false;
            }
        }
        return true;
    }

    std::wstring FlagToolWindow::BigUInt::ToDecimal() const
    {
        if (IsZero())
        {
            return L"0";
        }

        std::vector<std::uint32_t> value = limbs;
        std::wstring digits;
        while (!value.empty())
        {
            std::uint64_t remainder = 0;
            for (std::size_t index = value.size(); index-- > 0;)
            {
                const std::uint64_t current = (remainder << 32U) | value[index];
                value[index] = static_cast<std::uint32_t>(current / 10U);
                remainder = current % 10U;
            }
            digits.push_back(static_cast<wchar_t>(L'0' + remainder));
            while (!value.empty() && value.back() == 0U)
            {
                value.pop_back();
            }
        }
        std::reverse(digits.begin(), digits.end());
        return digits;
    }

    std::wstring FlagToolWindow::BigUInt::ToHex() const
    {
        if (IsZero())
        {
            return L"0";
        }

        std::wstringstream stream;
        stream << std::uppercase << std::hex;
        const std::size_t highest = limbs.size() - 1U;
        stream << limbs[highest];
        for (std::size_t index = highest; index-- > 0;)
        {
            stream.width(8);
            stream.fill(L'0');
            stream << limbs[index];
        }
        return stream.str();
    }

    bool FlagToolWindow::BigUInt::ParseDecimal(const std::wstring& text, BigUInt& value)
    {
        if (text.empty())
        {
            return false;
        }

        value.Clear();
        value.limbs.push_back(0U);
        for (const wchar_t character : text)
        {
            if (character < L'0' || character > L'9')
            {
                value.Clear();
                return false;
            }

            std::uint64_t carry = static_cast<std::uint64_t>(character - L'0');
            for (std::size_t index = 0; index < value.limbs.size(); ++index)
            {
                const std::uint64_t current =
                    static_cast<std::uint64_t>(value.limbs[index]) * 10ULL + carry;
                value.limbs[index] = static_cast<std::uint32_t>(current & 0xFFFFFFFFULL);
                carry = current >> 32U;
            }
            if (carry != 0U)
            {
                value.limbs.push_back(static_cast<std::uint32_t>(carry));
            }
        }
        value.Normalize();
        return true;
    }

    bool FlagToolWindow::BigUInt::ParseHex(const std::wstring& text, BigUInt& value)
    {
        if (text.empty())
        {
            return false;
        }

        std::size_t offset = 0;
        if (text.size() >= 2U && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X'))
        {
            offset = 2U;
        }
        if (offset >= text.size())
        {
            return false;
        }

        value.Clear();
        value.limbs.push_back(0U);
        for (std::size_t position = offset; position < text.size(); ++position)
        {
            const wchar_t character = text[position];
            unsigned int nibble = 0;
            if (character >= L'0' && character <= L'9')
            {
                nibble = static_cast<unsigned int>(character - L'0');
            }
            else if (character >= L'a' && character <= L'f')
            {
                nibble = 10U + static_cast<unsigned int>(character - L'a');
            }
            else if (character >= L'A' && character <= L'F')
            {
                nibble = 10U + static_cast<unsigned int>(character - L'A');
            }
            else
            {
                value.Clear();
                return false;
            }

            std::uint64_t carry = nibble;
            for (std::size_t index = 0; index < value.limbs.size(); ++index)
            {
                const std::uint64_t current =
                    (static_cast<std::uint64_t>(value.limbs[index]) << 4U) | carry;
                value.limbs[index] = static_cast<std::uint32_t>(current & 0xFFFFFFFFULL);
                carry = current >> 32U;
            }
            if (carry != 0U)
            {
                value.limbs.push_back(static_cast<std::uint32_t>(carry));
            }
        }
        value.Normalize();
        return true;
    }

    FlagToolWindow::~FlagToolWindow()
    {
        Destroy();
    }

    bool FlagToolWindow::Show(HINSTANCE instance, HWND ownerWindow)
    {
        if (_window == nullptr && !Create(instance, ownerWindow))
        {
            return false;
        }

        if (IsIconic(_window) != FALSE)
        {
            ShowWindow(_window, SW_RESTORE);
        }
        else
        {
            ShowWindow(_window, SW_SHOWNORMAL);
        }
        SetForegroundWindow(_window);
        BringWindowToTop(_window);
        return true;
    }

    void FlagToolWindow::Destroy()
    {
        DestroyFlagButtons();
        if (_window != nullptr && IsWindow(_window) != FALSE)
        {
            DestroyWindow(_window);
        }

        _window = nullptr;
        _toolLabel = nullptr;
        _tabSelector = nullptr;
        _resultEdit = nullptr;
        _checkButton = nullptr;
        _copyButton = nullptr;
        _markAllButton = nullptr;
        _clearAllButton = nullptr;
        _tabs.clear();
        _nullCellRects.clear();

        if (_backgroundBrush != nullptr)
        {
            DeleteObject(_backgroundBrush);
            _backgroundBrush = nullptr;
        }
        if (_headerBrush != nullptr)
        {
            DeleteObject(_headerBrush);
            _headerBrush = nullptr;
        }
        if (_cardBrush != nullptr)
        {
            DeleteObject(_cardBrush);
            _cardBrush = nullptr;
        }
        DestroyFonts();
        _instance = nullptr;
        _ownerWindow = nullptr;
    }

    bool FlagToolWindow::IsCreated() const
    {
        return _window != nullptr && IsWindow(_window) != FALSE;
    }

    LRESULT CALLBACK FlagToolWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        FlagToolWindow* self = reinterpret_cast<FlagToolWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<FlagToolWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            if (self != nullptr)
            {
                self->_window = window;
            }
        }

        if (self != nullptr)
        {
            const LRESULT result = self->HandleMessage(message, wParam, lParam);
            if (message == WM_NCDESTROY)
            {
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                self->_window = nullptr;
            }
            return result;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT FlagToolWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            InitializeFonts();
            InitializeTabs();
            CreatePersistentControls();
            RebuildCurrentTab();
            LayoutControls();
            UpdateResultEdit();
            return 0;

        case WM_CLOSE:
            ShowWindow(_window, SW_HIDE);
            return 0;

        case WM_SIZE:
            LayoutControls();
            RebuildCurrentTab();
            return 0;

        case WM_DPICHANGED:
        {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr)
            {
                SetWindowPos(
                    _window,
                    nullptr,
                    suggested->left,
                    suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOACTIVATE | SWP_NOZORDER);
            }
            DestroyFonts();
            InitializeFonts();
            if (_toolLabel != nullptr)
            {
                SendMessageW(_toolLabel, WM_SETFONT, reinterpret_cast<WPARAM>(_fontBold), TRUE);
            }
            if (_tabSelector != nullptr)
            {
                SendMessageW(_tabSelector, WM_SETFONT, reinterpret_cast<WPARAM>(_fontBold), TRUE);
            }
            if (_resultEdit != nullptr)
            {
                SendMessageW(_resultEdit, WM_SETFONT, reinterpret_cast<WPARAM>(_fontEntry), TRUE);
            }
            const std::array<HWND, 4> actionButtons =
            {
                _checkButton,
                _copyButton,
                _markAllButton,
                _clearAllButton,
            };
            for (HWND button : actionButtons)
            {
                if (button != nullptr)
                {
                    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(_fontBold), TRUE);
                }
            }
            LayoutControls();
            RebuildCurrentTab();
            return 0;
        }

        case WM_COMMAND:
        {
            const int controlId = LOWORD(wParam);
            const int notification = HIWORD(wParam);
            if (notification == BN_CLICKED)
            {
                if (controlId == IdTabSelector)
                {
                    ShowTabMenu();
                    return 0;
                }
                if (controlId == IdCheck)
                {
                    CheckCurrentInput();
                    return 0;
                }
                if (controlId == IdCopy)
                {
                    CopyCurrentResult();
                    return 0;
                }
                if (controlId == IdMarkAll)
                {
                    MarkAllCurrent();
                    return 0;
                }
                if (controlId == IdClearAll)
                {
                    ClearAllCurrent();
                    return 0;
                }
                if (controlId >= IdFlagBase)
                {
                    const std::size_t optionIndex = static_cast<std::size_t>(controlId - IdFlagBase);
                    if (optionIndex < CurrentTab().options.size())
                    {
                        ToggleOption(optionIndex);
                    }
                    return 0;
                }
            }
            break;
        }

        case WM_DRAWITEM:
        {
            const auto* drawItem = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (drawItem != nullptr)
            {
                DrawOwnerButton(*drawItem);
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
            return reinterpret_cast<LRESULT>(HandleControlColor(
                reinterpret_cast<HDC>(wParam),
                reinterpret_cast<HWND>(lParam),
                message));

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(_window, &paint);
            const RECT client = ClientRect();
            PaintBackground(dc, client);
            EndPaint(_window, &paint);
            return 0;
        }
        }

        return DefWindowProcW(_window, message, wParam, lParam);
    }

    bool FlagToolWindow::Create(HINSTANCE instance, HWND ownerWindow)
    {
        _instance = instance;
        _ownerWindow = ownerWindow;

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = _instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = FlagToolWindowClass;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }

        _backgroundBrush = CreateSolidBrush(ColorBackground);
        _headerBrush = CreateSolidBrush(ColorHeader);
        _cardBrush = CreateSolidBrush(ColorCard);
        if (_backgroundBrush == nullptr || _headerBrush == nullptr || _cardBrush == nullptr)
        {
            Destroy();
            return false;
        }

        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        const DWORD exStyle = WS_EX_CONTROLPARENT;
        RECT target{0, 0, Scale(WindowWidth), Scale(WindowHeight)};
        AdjustWindowRectEx(&target, style, FALSE, exStyle);
        int width = target.right - target.left;
        int height = target.bottom - target.top;

        HMONITOR monitor = MonitorFromWindow(ownerWindow, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (GetMonitorInfoW(monitor, &monitorInfo) != FALSE)
        {
            const int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
            const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
            width = std::min(width, std::max(800, workWidth - Scale(24)));
            height = std::min(height, std::max(520, workHeight - Scale(24)));
        }

        _window = CreateWindowExW(
            exStyle,
            FlagToolWindowClass,
            L"Flags Calculator",
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            width,
            height,
            ownerWindow,
            nullptr,
            _instance,
            this);
        if (_window == nullptr)
        {
            Destroy();
            return false;
        }

        CenterOnOwner();
        return true;
    }

    void FlagToolWindow::InitializeFonts()
    {
        _fontDefault = CreateSegoeUiFont(_window != nullptr ? _window : _ownerWindow, 10, false);
        _fontBold = CreateSegoeUiFont(_window != nullptr ? _window : _ownerWindow, 10, true);
        _fontEntry = CreateSegoeUiFont(_window != nullptr ? _window : _ownerWindow, 12, false);
        _fontCell10 = CreateSegoeUiFont(_window != nullptr ? _window : _ownerWindow, 10, false);
        _fontCell10Bold = CreateSegoeUiFont(_window != nullptr ? _window : _ownerWindow, 10, true);
        _fontCell9 = CreateSegoeUiFont(_window != nullptr ? _window : _ownerWindow, 9, false);
        _fontCell9Bold = CreateSegoeUiFont(_window != nullptr ? _window : _ownerWindow, 9, true);
        _fontCell8 = CreateSegoeUiFont(_window != nullptr ? _window : _ownerWindow, 8, false);
        _fontCell8Bold = CreateSegoeUiFont(_window != nullptr ? _window : _ownerWindow, 8, true);
    }

    void FlagToolWindow::DestroyFonts()
    {
        const std::array<HFONT*, 9> fonts =
        {
            &_fontDefault,
            &_fontBold,
            &_fontEntry,
            &_fontCell10,
            &_fontCell10Bold,
            &_fontCell9,
            &_fontCell9Bold,
            &_fontCell8,
            &_fontCell8Bold,
        };
        for (HFONT* font : fonts)
        {
            if (*font != nullptr)
            {
                DeleteObject(*font);
                *font = nullptr;
            }
        }
    }

    void FlagToolWindow::InitializeTabs()
    {
        _tabs.clear();
        _tabs.resize(TabNames.size());
        for (std::size_t index = 0; index < _tabs.size(); ++index)
        {
            _tabs[index].displayName = FormatDisplayText(TabNames[index]);
            _tabs[index].hexadecimal = index == 2U;
        }
        _currentTabIndex = 0;
        InitializeTab(_currentTabIndex);
    }

    void FlagToolWindow::InitializeTab(std::size_t tabIndex)
    {
        if (tabIndex >= _tabs.size() || _tabs[tabIndex].initialized)
        {
            return;
        }

        TabState& tab = _tabs[tabIndex];
        auto appendSpecs = [&tab](SpecView view, bool hexadecimal)
        {
            for (std::size_t index = 0; index < view.size; ++index)
            {
                OptionState state;
                state.name = FormatDisplayText(view.data[index].name);
                const std::wstring valueText = view.data[index].value;
                const bool parsed = hexadecimal
                    ? BigUInt::ParseHex(valueText, state.value)
                    : BigUInt::ParseDecimal(valueText, state.value);
                if (!parsed)
                {
                    state.value.Clear();
                }
                tab.options.push_back(std::move(state));
            }
        };

        switch (tabIndex)
        {
        case 0:
            appendSpecs(View(ItemFlags), false);
            break;
        case 1:
            appendSpecs(View(ItemPlusFlags), false);
            break;
        case 2:
            for (const SpecView column : ClassColumns)
            {
                appendSpecs(column, true);
            }
            break;
        case 3:
            appendSpecs(View(EnchantFlags), false);
            break;
        case 4:
            appendSpecs(View(SpellOpFlagFlags), false);
            break;
        case 5:
            appendSpecs(View(SpellRestrictEquipFlags), false);
            break;
        case 6:
            appendSpecs(View(MissionFlags), false);
            break;
        case 7:
            appendSpecs(View(MonsterSpecialFlagFlags), false);
            break;
        default:
            break;
        }
        tab.initialized = true;
    }

    void FlagToolWindow::CreatePersistentControls()
    {
        _toolLabel = CreateWindowExW(
            0,
            L"STATIC",
            L"Tool",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0,
            0,
            0,
            0,
            _window,
            nullptr,
            _instance,
            nullptr);

        _tabSelector = CreateWindowExW(
            0,
            L"BUTTON",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            _window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdTabSelector)),
            _instance,
            nullptr);

        _checkButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Check",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            _window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdCheck)),
            _instance,
            nullptr);

        _resultEdit = CreateWindowExW(
            0,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            _window,
            nullptr,
            _instance,
            nullptr);

        _copyButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Copy",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            _window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdCopy)),
            _instance,
            nullptr);

        _markAllButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Mark All",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            _window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdMarkAll)),
            _instance,
            nullptr);

        _clearAllButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Clear All",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            _window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdClearAll)),
            _instance,
            nullptr);

        if (_toolLabel != nullptr)
        {
            SendMessageW(_toolLabel, WM_SETFONT, reinterpret_cast<WPARAM>(_fontBold), TRUE);
        }
        if (_tabSelector != nullptr)
        {
            SendMessageW(_tabSelector, WM_SETFONT, reinterpret_cast<WPARAM>(_fontBold), TRUE);
        }
        if (_resultEdit != nullptr)
        {
            SendMessageW(_resultEdit, WM_SETFONT, reinterpret_cast<WPARAM>(_fontEntry), TRUE);
            SendMessageW(_resultEdit, EM_SETLIMITTEXT, 4096, 0);
        }
        const std::array<HWND, 4> actionButtons =
        {
            _checkButton,
            _copyButton,
            _markAllButton,
            _clearAllButton,
        };
        for (HWND button : actionButtons)
        {
            if (button != nullptr)
            {
                SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(_fontBold), TRUE);
            }
        }
    }

    void FlagToolWindow::RebuildCurrentTab()
    {
        if (_window == nullptr || _tabs.empty())
        {
            return;
        }

        DestroyFlagButtons();
        _nullCellRects.clear();
        InitializeTab(_currentTabIndex);

        const RECT content = ContentRect();
        if (_currentTabIndex == 2U)
        {
            BuildClassGrid(content);
        }
        else
        {
            BuildDecimalGrid(content);
        }
        InvalidateRect(_window, &content, TRUE);
    }

    void FlagToolWindow::DestroyFlagButtons()
    {
        for (const VisibleFlagButton& flag : _visibleFlagButtons)
        {
            if (flag.window != nullptr && IsWindow(flag.window) != FALSE)
            {
                DestroyWindow(flag.window);
            }
        }
        _visibleFlagButtons.clear();
    }

    void FlagToolWindow::LayoutControls()
    {
        if (_window == nullptr)
        {
            return;
        }

        const RECT client = ClientRect();
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const int toolbarHeight = Scale(ToolbarHeight);
        const int statusbarHeight = Scale(StatusbarHeight);

        const int toolbarPadX = Scale(10);
        const int toolbarPadY = Scale(7);
        const int labelWidth = Scale(38);
        const int selectorWidth = Scale(205);
        const int selectorHeight = std::max(Scale(28), toolbarHeight - (toolbarPadY * 2));
        const int selectorTop = std::max(0, (toolbarHeight - selectorHeight) / 2);

        if (_toolLabel != nullptr)
        {
            MoveWindow(_toolLabel, toolbarPadX, 0, labelWidth, toolbarHeight, TRUE);
        }
        if (_tabSelector != nullptr)
        {
            MoveWindow(
                _tabSelector,
                toolbarPadX + labelWidth + Scale(8),
                selectorTop,
                selectorWidth,
                selectorHeight,
                TRUE);
        }

        const int buttonHeight = Scale(30);
        const int checkWidth = Scale(94);
        const int entryWidth = Scale(205);
        const int copyWidth = Scale(94);
        const int markWidth = Scale(108);
        const int clearWidth = Scale(108);
        const int gap = Scale(3);
        const int totalWidth = checkWidth + entryWidth + copyWidth + markWidth + clearWidth + gap * 8;
        int x = std::max(Scale(8), (width - totalWidth) / 2);
        const int y = height - statusbarHeight + std::max(0, (statusbarHeight - buttonHeight) / 2);

        auto place = [&](HWND control, int controlWidth)
        {
            if (control != nullptr)
            {
                MoveWindow(control, x, y, controlWidth, buttonHeight, TRUE);
            }
            x += controlWidth + gap * 2;
        };

        place(_checkButton, checkWidth);
        place(_resultEdit, entryWidth);
        place(_copyButton, copyWidth);
        place(_markAllButton, markWidth);
        place(_clearAllButton, clearWidth);
    }

    void FlagToolWindow::BuildDecimalGrid(const RECT& contentRect)
    {
        TabState& tab = CurrentTab();
        const std::size_t count = tab.options.size();
        if (count == 0U)
        {
            return;
        }

        constexpr int cellsPerRow = 4;
        const int rows = static_cast<int>((count + cellsPerRow - 1U) / cellsPerRow);
        const int availableWidth = std::max(1, static_cast<int>(contentRect.right - contentRect.left));
        const int availableHeight = std::max(1, static_cast<int>(contentRect.bottom - contentRect.top));
        const int padding = Scale(CellPadding);
        const int desiredCellWidth = Scale(CellWidth);
        const int desiredCellHeight = Scale(CellHeight);
        const int cellWidth = std::max(
            Scale(88),
            std::min(desiredCellWidth, (availableWidth - padding * cellsPerRow * 2) / cellsPerRow));
        const int cellHeight = std::max(
            Scale(30),
            std::min(desiredCellHeight, (availableHeight - padding * rows * 2) / std::max(1, rows)));
        const int gridWidth = cellsPerRow * (cellWidth + padding * 2);
        const int gridHeight = rows * (cellHeight + padding * 2);
        const int startX = contentRect.left + std::max(0, (availableWidth - gridWidth) / 2);
        const int startY = contentRect.top + std::max(0, (availableHeight - gridHeight) / 2);

        for (std::size_t index = 0; index < count; ++index)
        {
            const int row = static_cast<int>(index % static_cast<std::size_t>(rows));
            const int column = static_cast<int>(index / static_cast<std::size_t>(rows));
            RECT cell{};
            cell.left = startX + column * (cellWidth + padding * 2) + padding;
            cell.top = startY + row * (cellHeight + padding * 2) + padding;
            cell.right = cell.left + cellWidth;
            cell.bottom = cell.top + cellHeight;
            CreateFlagButton(index, cell, false);
        }
    }

    void FlagToolWindow::BuildClassGrid(const RECT& contentRect)
    {
        TabState& tab = CurrentTab();
        std::size_t maxRows = 0;
        for (const SpecView column : ClassColumns)
        {
            maxRows = std::max(maxRows, column.size);
        }
        if (maxRows == 0U)
        {
            return;
        }

        const int columnCount = static_cast<int>(ClassColumns.size());
        const int rowCount = static_cast<int>(maxRows);
        const int availableWidth = std::max(1, static_cast<int>(contentRect.right - contentRect.left));
        const int availableHeight = std::max(1, static_cast<int>(contentRect.bottom - contentRect.top));
        const int padding = Scale(CellPadding);
        const int desiredCellWidth = Scale(CellWidth);
        const int desiredCellHeight = Scale(CellHeight);
        const int cellWidth = std::max(
            Scale(64),
            std::min(desiredCellWidth, (availableWidth - padding * columnCount * 2) / std::max(1, columnCount)));
        const int cellHeight = std::max(
            Scale(28),
            std::min(desiredCellHeight, (availableHeight - padding * rowCount * 2) / std::max(1, rowCount)));
        const int gridWidth = columnCount * (cellWidth + padding * 2);
        const int gridHeight = rowCount * (cellHeight + padding * 2);
        const int startX = contentRect.left + std::max(0, (availableWidth - gridWidth) / 2);
        const int startY = contentRect.top + std::max(0, (availableHeight - gridHeight) / 2);

        std::size_t optionOffset = 0;
        for (int column = 0; column < columnCount; ++column)
        {
            const SpecView columnSpecs = ClassColumns[static_cast<std::size_t>(column)];
            for (int row = 0; row < rowCount; ++row)
            {
                RECT cell{};
                cell.left = startX + column * (cellWidth + padding * 2) + padding;
                cell.top = startY + row * (cellHeight + padding * 2) + padding;
                cell.right = cell.left + cellWidth;
                cell.bottom = cell.top + cellHeight;

                if (static_cast<std::size_t>(row) < columnSpecs.size)
                {
                    const std::size_t optionIndex = optionOffset + static_cast<std::size_t>(row);
                    if (optionIndex < tab.options.size())
                    {
                        CreateFlagButton(optionIndex, cell, column == 0);
                    }
                }
                else
                {
                    _nullCellRects.push_back(cell);
                }
            }
            optionOffset += columnSpecs.size;
        }
    }

    void FlagToolWindow::CreateFlagButton(std::size_t optionIndex, const RECT& rect, bool bold)
    {
        if (optionIndex >= CurrentTab().options.size())
        {
            return;
        }

        HWND button = CreateWindowExW(
            0,
            L"BUTTON",
            CurrentTab().options[optionIndex].name.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            rect.left,
            rect.top,
            rect.right - rect.left,
            rect.bottom - rect.top,
            _window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdFlagBase + optionIndex)),
            _instance,
            nullptr);
        if (button == nullptr)
        {
            return;
        }
        SendMessageW(
            button,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(CellFontForText(CurrentTab().options[optionIndex].name, bold)),
            TRUE);
        _visibleFlagButtons.push_back({button, optionIndex, bold});
    }

    void FlagToolWindow::SwitchTab(std::size_t tabIndex)
    {
        if (tabIndex >= _tabs.size())
        {
            return;
        }
        InitializeTab(tabIndex);
        _currentTabIndex = tabIndex;
        RebuildCurrentTab();
        UpdateResultEdit();
        if (_tabSelector != nullptr)
        {
            InvalidateRect(_tabSelector, nullptr, TRUE);
        }
    }

    void FlagToolWindow::ShowTabMenu()
    {
        if (_tabSelector == nullptr)
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
        {
            return;
        }
        for (std::size_t index = 0; index < TabNames.size(); ++index)
        {
            AppendMenuW(
                menu,
                MF_STRING,
                static_cast<UINT_PTR>(IdTabMenuBase + index),
                _tabs[index].displayName.c_str());
        }

        RECT selectorRect{};
        GetWindowRect(_tabSelector, &selectorRect);
        const UINT command = TrackPopupMenuEx(
            menu,
            TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
            selectorRect.left,
            selectorRect.bottom,
            _window,
            nullptr);
        DestroyMenu(menu);

        if (command >= static_cast<UINT>(IdTabMenuBase) &&
            command < static_cast<UINT>(IdTabMenuBase + TabNames.size()))
        {
            SwitchTab(static_cast<std::size_t>(command - IdTabMenuBase));
        }
    }

    void FlagToolWindow::ToggleOption(std::size_t optionIndex)
    {
        TabState& tab = CurrentTab();
        if (optionIndex >= tab.options.size())
        {
            return;
        }

        OptionState& option = tab.options[optionIndex];
        if (option.selected)
        {
            option.selected = false;
        }
        else
        {
            if (option.value.IsZero())
            {
                for (OptionState& item : tab.options)
                {
                    item.selected = false;
                }
            }
            else
            {
                for (OptionState& item : tab.options)
                {
                    if (item.value.IsZero())
                    {
                        item.selected = false;
                    }
                }
            }
            option.selected = true;
        }

        RecalculateCurrentTotal();
        UpdateResultEdit();
        for (const VisibleFlagButton& flag : _visibleFlagButtons)
        {
            InvalidateRect(flag.window, nullptr, FALSE);
        }
    }

    void FlagToolWindow::RecalculateCurrentTotal()
    {
        TabState& tab = CurrentTab();
        tab.total.Clear();
        for (const OptionState& option : tab.options)
        {
            if (option.selected)
            {
                tab.total.OrWith(option.value);
            }
        }
    }

    void FlagToolWindow::CheckCurrentInput()
    {
        const std::wstring input = GetTrimmedEditText();
        TabState& tab = CurrentTab();
        BigUInt parsed;
        const bool valid = tab.hexadecimal
            ? BigUInt::ParseHex(input, parsed)
            : BigUInt::ParseDecimal(input, parsed);
        if (!valid)
        {
            MessageBoxW(
                _window,
                tab.hexadecimal ? L"Invalid hexadecimal value." : L"Invalid decimal value.",
                L"Error",
                MB_OK | MB_ICONWARNING);
            return;
        }

        bool hasMatchedFlag = false;
        bool hasZeroFlag = false;
        for (OptionState& option : tab.options)
        {
            const bool zero = option.value.IsZero();
            hasZeroFlag = hasZeroFlag || zero;
            const bool match = zero ? parsed.IsZero() : parsed.ContainsBits(option.value);
            option.selected = match;
            if (match && !zero)
            {
                hasMatchedFlag = true;
            }
        }

        if (!hasMatchedFlag && !parsed.IsZero())
        {
            MessageBoxW(_window, L"No flag matches.", L"Info", MB_OK | MB_ICONINFORMATION);
        }
        if (parsed.IsZero() && !hasZeroFlag)
        {
            for (OptionState& option : tab.options)
            {
                option.selected = false;
            }
        }

        tab.total = parsed;
        UpdateResultEdit();
        for (const VisibleFlagButton& flag : _visibleFlagButtons)
        {
            InvalidateRect(flag.window, nullptr, FALSE);
        }
    }

    void FlagToolWindow::MarkAllCurrent()
    {
        TabState& tab = CurrentTab();
        bool hasNonZero = false;
        for (OptionState& option : tab.options)
        {
            if (!option.value.IsZero())
            {
                option.selected = true;
                hasNonZero = true;
            }
            else
            {
                option.selected = false;
            }
        }
        if (!hasNonZero)
        {
            for (OptionState& option : tab.options)
            {
                option.selected = true;
            }
        }
        RecalculateCurrentTotal();
        UpdateResultEdit();
        for (const VisibleFlagButton& flag : _visibleFlagButtons)
        {
            InvalidateRect(flag.window, nullptr, FALSE);
        }
    }

    void FlagToolWindow::ClearAllCurrent()
    {
        TabState& tab = CurrentTab();
        for (OptionState& option : tab.options)
        {
            option.selected = false;
        }
        tab.total.Clear();
        UpdateResultEdit();
        for (const VisibleFlagButton& flag : _visibleFlagButtons)
        {
            InvalidateRect(flag.window, nullptr, FALSE);
        }
    }

    void FlagToolWindow::CopyCurrentResult()
    {
        const std::wstring value = CurrentResultText();
        const std::wstring copyValue = value.empty() ? L"0" : value;
        bool copied = false;
        if (OpenClipboard(_window) != FALSE)
        {
            EmptyClipboard();
            const std::size_t bytes = (copyValue.size() + 1U) * sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (memory != nullptr)
            {
                void* target = GlobalLock(memory);
                if (target != nullptr)
                {
                    memcpy(target, copyValue.c_str(), bytes);
                    GlobalUnlock(memory);
                    if (SetClipboardData(CF_UNICODETEXT, memory) != nullptr)
                    {
                        copied = true;
                        memory = nullptr;
                    }
                }
                if (memory != nullptr)
                {
                    GlobalFree(memory);
                }
            }
            CloseClipboard();
        }

        if (copied)
        {
            const std::wstring message = L"Value " + copyValue + L" copied!";
            MessageBoxW(_window, message.c_str(), L"Success", MB_OK | MB_ICONINFORMATION);
        }
    }

    void FlagToolWindow::UpdateResultEdit()
    {
        if (_resultEdit == nullptr)
        {
            return;
        }
        const std::wstring text = CurrentResultText();
        SetWindowTextW(_resultEdit, text.c_str());
    }

    std::wstring FlagToolWindow::CurrentResultText() const
    {
        if (_tabs.empty())
        {
            return {};
        }

        const TabState& tab = CurrentTab();
        bool explicitZero = false;
        for (const OptionState& option : tab.options)
        {
            if (option.selected && option.value.IsZero())
            {
                explicitZero = true;
                break;
            }
        }
        if (tab.total.IsZero() && !explicitZero)
        {
            return {};
        }
        return tab.hexadecimal ? tab.total.ToHex() : tab.total.ToDecimal();
    }

    std::wstring FlagToolWindow::GetTrimmedEditText() const
    {
        if (_resultEdit == nullptr)
        {
            return {};
        }
        const int length = GetWindowTextLengthW(_resultEdit);
        std::wstring text(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
        const int copied = GetWindowTextW(_resultEdit, text.data(), length + 1);
        text.resize(static_cast<std::size_t>(std::max(0, copied)));
        return Trim(text);
    }

    void FlagToolWindow::CenterOnOwner()
    {
        if (_window == nullptr)
        {
            return;
        }

        RECT windowRect{};
        GetWindowRect(_window, &windowRect);
        RECT ownerRect{};
        if (_ownerWindow == nullptr || GetWindowRect(_ownerWindow, &ownerRect) == FALSE)
        {
            HMONITOR monitor = MonitorFromWindow(_window, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) != FALSE)
            {
                ownerRect = info.rcWork;
            }
        }

        const int width = windowRect.right - windowRect.left;
        const int height = windowRect.bottom - windowRect.top;
        int left = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        int top = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

        HMONITOR monitor = MonitorFromRect(&ownerRect, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) != FALSE)
        {
            const int workLeft = static_cast<int>(info.rcWork.left);
            const int workTop = static_cast<int>(info.rcWork.top);
            const int workRight = static_cast<int>(info.rcWork.right);
            const int workBottom = static_cast<int>(info.rcWork.bottom);
            left = std::clamp(left, workLeft, std::max(workLeft, workRight - width));
            top = std::clamp(top, workTop, std::max(workTop, workBottom - height));
        }
        SetWindowPos(_window, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void FlagToolWindow::PaintBackground(HDC dc, const RECT& clientRect)
    {
        FillRect(dc, &clientRect, _backgroundBrush);

        RECT header = clientRect;
        header.bottom = std::min<LONG>(clientRect.bottom, static_cast<LONG>(Scale(ToolbarHeight)));
        FillRect(dc, &header, _headerBrush);

        RECT status = clientRect;
        status.top = std::max<LONG>(
            clientRect.top,
            clientRect.bottom - static_cast<LONG>(Scale(StatusbarHeight)));
        FillRect(dc, &status, _headerBrush);

        for (const RECT& nullCell : _nullCellRects)
        {
            FillSolidRect(dc, nullCell, ColorFrameNull);
        }
    }

    void FlagToolWindow::DrawOwnerButton(const DRAWITEMSTRUCT& drawItem)
    {
        if (drawItem.hwndItem == _tabSelector)
        {
            DrawTabSelector(drawItem);
            return;
        }
        if (const VisibleFlagButton* flag = FindVisibleFlagButton(drawItem.hwndItem))
        {
            DrawFlagButton(drawItem, *flag);
            return;
        }

        if (drawItem.hwndItem == _checkButton)
        {
            DrawActionButton(drawItem, L"Check");
        }
        else if (drawItem.hwndItem == _copyButton)
        {
            DrawActionButton(drawItem, L"Copy");
        }
        else if (drawItem.hwndItem == _markAllButton)
        {
            DrawActionButton(drawItem, L"Mark All");
        }
        else if (drawItem.hwndItem == _clearAllButton)
        {
            DrawActionButton(drawItem, L"Clear All");
        }
    }

    void FlagToolWindow::DrawFlagButton(
        const DRAWITEMSTRUCT& drawItem,
        const VisibleFlagButton& flagButton)
    {
        const TabState& tab = CurrentTab();
        if (flagButton.optionIndex >= tab.options.size())
        {
            return;
        }

        const OptionState& option = tab.options[flagButton.optionIndex];
        const bool selected = option.selected;
        const bool pressed = (drawItem.itemState & ODS_SELECTED) != 0;
        const COLORREF background = selected ? ColorFrameSelected : ColorFrameNormal;
        FillSolidRect(drawItem.hDC, drawItem.rcItem, background);
        FrameSolidRect(
            drawItem.hDC,
            drawItem.rcItem,
            pressed ? ColorHighlight : (selected ? ColorFrameSelected : ColorBorder));

        RECT textRect = drawItem.rcItem;
        InflateRect(&textRect, -Scale(8), -Scale(4));
        DrawCenteredWrappedText(
            drawItem.hDC,
            textRect,
            option.name,
            CellFontForText(option.name, flagButton.bold),
            selected ? ColorFrameText : ColorText);
    }

    void FlagToolWindow::DrawActionButton(
        const DRAWITEMSTRUCT& drawItem,
        const wchar_t* text,
        bool active)
    {
        const bool pressed = (drawItem.itemState & ODS_SELECTED) != 0;
        const COLORREF background = (pressed || active) ? ColorHighlight : ColorButton;
        const COLORREF foreground = (pressed || active) ? ColorButtonActiveText : ColorButtonText;
        FillSolidRect(drawItem.hDC, drawItem.rcItem, background);
        FrameSolidRect(drawItem.hDC, drawItem.rcItem, pressed ? ColorHighlight : ColorBorder);

        SetBkMode(drawItem.hDC, TRANSPARENT);
        SetTextColor(drawItem.hDC, foreground);
        HFONT font = _fontBold != nullptr ? _fontBold : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ oldFont = SelectObject(drawItem.hDC, font);
        RECT rect = drawItem.rcItem;
        DrawTextW(drawItem.hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(drawItem.hDC, oldFont);
    }

    void FlagToolWindow::DrawTabSelector(const DRAWITEMSTRUCT& drawItem)
    {
        const bool pressed = (drawItem.itemState & ODS_SELECTED) != 0;
        FillSolidRect(drawItem.hDC, drawItem.rcItem, pressed ? ColorHighlight : ColorButton);
        FrameSolidRect(drawItem.hDC, drawItem.rcItem, pressed ? ColorHighlight : ColorBorder);

        SetBkMode(drawItem.hDC, TRANSPARENT);
        SetTextColor(drawItem.hDC, pressed ? ColorButtonActiveText : ColorButtonText);
        HFONT font = _fontBold != nullptr ? _fontBold : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ oldFont = SelectObject(drawItem.hDC, font);
        RECT rect = drawItem.rcItem;
        rect.left += Scale(10);
        rect.right -= Scale(10);
        const std::wstring text = CurrentTab().displayName + L"  \x25BE";
        DrawTextW(drawItem.hDC, text.c_str(), -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(drawItem.hDC, oldFont);
    }

    void FlagToolWindow::DrawCenteredWrappedText(
        HDC dc,
        const RECT& rect,
        const std::wstring& text,
        HFONT font,
        COLORREF color)
    {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        if (font == nullptr)
        {
            font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        HGDIOBJ oldFont = SelectObject(dc, font);

        RECT measure = rect;
        DrawTextW(
            dc,
            text.c_str(),
            -1,
            &measure,
            DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        const int textHeight = std::max(1, static_cast<int>(measure.bottom - measure.top));
        RECT draw = rect;
        const int rectHeight = static_cast<int>(rect.bottom - rect.top);
        draw.top += static_cast<LONG>(std::max(0, (rectHeight - textHeight) / 2));
        DrawTextW(dc, text.c_str(), -1, &draw, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    }

    HFONT FlagToolWindow::CellFontForText(const std::wstring& text, bool bold) const
    {
        int size = 10;
        if (text.size() > 24U)
        {
            --size;
        }
        if (text.size() > 34U)
        {
            --size;
        }
        if (size <= 8)
        {
            return bold ? _fontCell8Bold : _fontCell8;
        }
        if (size == 9)
        {
            return bold ? _fontCell9Bold : _fontCell9;
        }
        return bold ? _fontCell10Bold : _fontCell10;
    }

    HBRUSH FlagToolWindow::HandleControlColor(HDC dc, HWND control, UINT message)
    {
        SetBkMode(dc, OPAQUE);
        if (control == _toolLabel)
        {
            SetTextColor(dc, ColorHighlight);
            SetBkColor(dc, ColorHeader);
            return _headerBrush;
        }
        if (control == _resultEdit && message == WM_CTLCOLOREDIT)
        {
            SetTextColor(dc, ColorText);
            SetBkColor(dc, ColorCard);
            return _cardBrush;
        }
        SetTextColor(dc, ColorText);
        SetBkColor(dc, ColorBackground);
        return _backgroundBrush;
    }

    int FlagToolWindow::Scale(int value) const
    {
        UINT dpi = 96;
        using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFunction>(
            user32 != nullptr ? GetProcAddress(user32, "GetDpiForWindow") : nullptr);
        const HWND reference = _window != nullptr ? _window : _ownerWindow;
        if (getDpiForWindow != nullptr && reference != nullptr)
        {
            dpi = getDpiForWindow(reference);
        }
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    RECT FlagToolWindow::ClientRect() const
    {
        RECT rect{};
        if (_window != nullptr)
        {
            GetClientRect(_window, &rect);
        }
        return rect;
    }

    RECT FlagToolWindow::ContentRect() const
    {
        RECT rect = ClientRect();
        rect.top += Scale(ToolbarHeight);
        rect.bottom -= Scale(StatusbarHeight);
        if (rect.bottom < rect.top)
        {
            rect.bottom = rect.top;
        }
        return rect;
    }

    FlagToolWindow::TabState& FlagToolWindow::CurrentTab()
    {
        return _tabs[_currentTabIndex];
    }

    const FlagToolWindow::TabState& FlagToolWindow::CurrentTab() const
    {
        return _tabs[_currentTabIndex];
    }

    const FlagToolWindow::VisibleFlagButton* FlagToolWindow::FindVisibleFlagButton(HWND window) const
    {
        const auto found = std::find_if(
            _visibleFlagButtons.begin(),
            _visibleFlagButtons.end(),
            [window](const VisibleFlagButton& flag)
            {
                return flag.window == window;
            });
        return found != _visibleFlagButtons.end() ? &(*found) : nullptr;
    }

    std::wstring FlagToolWindow::Trim(const std::wstring& text)
    {
        std::size_t start = 0;
        while (start < text.size() && std::iswspace(text[start]) != 0)
        {
            ++start;
        }
        std::size_t end = text.size();
        while (end > start && std::iswspace(text[end - 1U]) != 0)
        {
            --end;
        }
        return text.substr(start, end - start);
    }
}
