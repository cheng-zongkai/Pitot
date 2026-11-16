// 两种语言的文字表都要填满：Strings 只由 const wchar_t* 组成，按指针数组逐个检查

#include <cwchar>
#include <cwctype>
#include <string>

#include "test.h"
#include "ui/strings.h"

namespace {

using pitot::ui::Language;
using pitot::ui::Strings;
using pitot::ui::StringsFor;

static_assert(sizeof(Strings) % sizeof(const wchar_t*) == 0);
constexpr size_t kCount = sizeof(Strings) / sizeof(const wchar_t*);

bool AllFilled(const Strings& s) {
    const auto* p = reinterpret_cast<const wchar_t* const*>(&s);
    for (size_t i = 0; i < kCount; ++i) {
        if (p[i] == nullptr || p[i][0] == L'\0') {
            return false;
        }
    }
    return true;
}

// 取出格式串里的转换说明（按出现顺序）。两种语言的同一项必须完全一致：
// 参数顺序不同（例如英文写 "%d ... %s"）会把 %s 喂给整数，运行时直接访问违例。
std::wstring FormatSpecs(const wchar_t* text) {
    std::wstring specs;
    if (!text) {
        return specs;
    }
    const wchar_t* p = text;
    while (*p) {
        if (*p != L'%') {
            ++p;
            continue;
        }
        if (p[1] == L'%') {
            p += 2;
            continue;
        }
        const wchar_t* start = p++;
        while (*p && wcschr(L"-+ #0'", *p)) {
            ++p;
        }
        while (*p && (iswdigit(*p) || *p == L'*')) {
            ++p;
        }
        if (*p == L'.') {
            ++p;
            while (*p && (iswdigit(*p) || *p == L'*')) {
                ++p;
            }
        }
        while (*p && wcschr(L"hljztL", *p)) {
            ++p;
        }
        if (!*p) {
            break;  // 格式串不完整，后面的文字也不再看了
        }
        ++p;  // 转换字符
        specs.append(start, static_cast<size_t>(p - start));
        specs.push_back(L'|');
    }
    return specs;
}

}  // namespace

TEST_CASE("strings: every entry is filled in both languages") {
    CHECK(AllFilled(StringsFor(Language::Chinese)));
    CHECK(AllFilled(StringsFor(Language::English)));
}

TEST_CASE("strings: format specifiers match between languages") {
    const auto* zh = reinterpret_cast<const wchar_t* const*>(&StringsFor(Language::Chinese));
    const auto* en = reinterpret_cast<const wchar_t* const*>(&StringsFor(Language::English));
    for (size_t i = 0; i < kCount; ++i) {
        const std::wstring zh_specs = FormatSpecs(zh[i]);
        const std::wstring en_specs = FormatSpecs(en[i]);
        if (zh_specs != en_specs) {
            std::printf("    field %zu: zh=[%ls] en=[%ls]\n", i, zh_specs.c_str(), en_specs.c_str());
        }
        CHECK(zh_specs == en_specs);
    }
}

TEST_CASE("strings: end process confirmations use name then count") {
    // 直接按代码里的参数顺序格式化两种语言，防止译文换了顺序（2026-11-05 崩溃过）
    for (Language language : {Language::Chinese, Language::English}) {
        pitot::ui::SetLanguage(language);
        wchar_t text[256];
        _snwprintf_s(text, _TRUNCATE, pitot::ui::Str().kEndConfirmGroup, L"Chrome", 3);
        CHECK(wcsstr(text, L"Chrome") != nullptr);
        CHECK(wcsstr(text, L"3") != nullptr);
        _snwprintf_s(text, _TRUNCATE, pitot::ui::Str().kEndDoneGroup, L"Chrome", 3);
        CHECK(wcsstr(text, L"Chrome") != nullptr);
        CHECK(wcsstr(text, L"3") != nullptr);
    }
    pitot::ui::SetLanguage(Language::Chinese);
}

TEST_CASE("strings: switching language") {
    pitot::ui::SetLanguage(Language::English);
    CHECK(wcscmp(pitot::ui::Str().kTileStorage, L"Storage") == 0);
    pitot::ui::SetLanguage(Language::Chinese);
    CHECK(wcscmp(pitot::ui::Str().kTileStorage, L"存储") == 0);
}