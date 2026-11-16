#include "ui/strip_metrics.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <initializer_list>

#include <wrl/client.h>

#include "ui/format.h"
#include "ui/strings.h"

namespace pitot::ui {
namespace {

using Microsoft::WRL::ComPtr;

// 和 strip_window 的文字格式一致：zh-CN、不换行。宽度按等宽数字量，数值变位数时列宽不变
const wchar_t* ResolveFamily(IDWriteFactory* factory, const wchar_t* wanted) {
    if (wanted && wanted[0]) {
        ComPtr<IDWriteFontCollection> fonts;
        if (SUCCEEDED(factory->GetSystemFontCollection(&fonts, FALSE))) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(fonts->FindFamilyName(wanted, &index, &exists)) && exists) {
                return wanted;
            }
        }
    }
    return L"Segoe UI";
}

HRESULT MakeFormat(IDWriteFactory* factory, const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight,
                   ComPtr<IDWriteTextFormat>& out) {
    HRESULT hr = factory->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                           size, L"zh-CN", out.ReleaseAndGetAddressOf());
    if (SUCCEEDED(hr)) {
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return hr;
}

float Measure(IDWriteFactory* factory, IDWriteTextFormat* format, IDWriteTypography* typography, const wchar_t* text) {
    if (!text || !text[0] || !format) {
        return 0.0f;
    }
    const auto length = static_cast<UINT32>(wcslen(text));
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(text, length, format, 1000.0f, 100.0f, &layout))) {
        return 0.0f;
    }
    layout->SetTypography(typography, {0, length});
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return std::ceil(metrics.widthIncludingTrailingWhitespace);
}

float Widest(IDWriteFactory* factory, IDWriteTextFormat* format, IDWriteTypography* typography,
             std::initializer_list<const wchar_t*> texts) {
    float width = 0.0f;
    for (const wchar_t* text : texts) {
        width = std::max(width, Measure(factory, format, typography, text));
    }
    return width;
}

}  // namespace

StripMeasured MeasureStripColumns(IDWriteFactory* factory, const config::StripStyle& style,
                                  const wchar_t* const labels[kStripItemCount],
                                  const wchar_t* const rate_prefixes[kRatePrefixCount], const wchar_t* unavailable) {
    StripMeasured out;
    if (!factory) {
        return out;
    }
    const wchar_t* family = ResolveFamily(factory, style.font);
    const auto weight = static_cast<DWRITE_FONT_WEIGHT>(std::clamp(style.value_weight, 1, 999));
    ComPtr<IDWriteTextFormat> label_format;
    ComPtr<IDWriteTextFormat> value_format;
    ComPtr<IDWriteTextFormat> net_format;
    ComPtr<IDWriteTextFormat> compact_label_format;
    ComPtr<IDWriteTextFormat> compact_value_format;
    ComPtr<IDWriteTypography> typography;
    if (FAILED(factory->CreateTypography(&typography)) ||
        FAILED(typography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1})) ||
        FAILED(MakeFormat(factory, family, style.label_size, DWRITE_FONT_WEIGHT_NORMAL, label_format)) ||
        FAILED(MakeFormat(factory, family, style.value_size, weight, value_format)) ||
        FAILED(MakeFormat(factory, family, style.net_size, DWRITE_FONT_WEIGHT_NORMAL, net_format)) ||
        FAILED(MakeFormat(factory, family, CompactSize(style), DWRITE_FONT_WEIGHT_NORMAL, compact_label_format)) ||
        FAILED(MakeFormat(factory, family, CompactSize(style), weight, compact_value_format))) {
        return out;
    }
    const auto prefix = [&](int i) { return rate_prefixes ? rate_prefixes[i] : nullptr; };

    // 两行的开头标记（↓ / ↑ / 读 / 写）取最宽的一个，数字从同一位置开始
    out.rate_prefix = 0.0f;
    for (int i = 0; i < 4; ++i) {
        out.rate_prefix = std::max(out.rate_prefix, Measure(factory, net_format.Get(), typography.Get(), prefix(i)));
    }
    out.rate_space = Measure(factory, net_format.Get(), typography.Get(), L" ");
    out.rate_prefix += out.rate_space;
    // 数字列按最宽的可能读数：自动单位最多 3 位；固定 KB/s 可能到 5 位，固定 MB/s 带两位小数（D-073）
    out.rate_number = Widest(factory, net_format.Get(), typography.Get(), {L"888", L"8.8", unavailable});
    if (style.net_unit == config::NetUnit::Kilo) {
        out.rate_number = std::max(out.rate_number, Measure(factory, net_format.Get(), typography.Get(), L"88888"));
    } else if (style.net_unit == config::NetUnit::Mega) {
        out.rate_number = std::max(out.rate_number, Measure(factory, net_format.Get(), typography.Get(), L"8.88"));
    }
    out.rate_unit = 0.0f;
    for (const wchar_t* unit : kAllRateUnits) {
        out.rate_unit = std::max(out.rate_unit, Measure(factory, net_format.Get(), typography.Get(), unit));
    }
    if (style.net_unit == config::NetUnit::Bits) {
        for (const wchar_t* unit : kAllBitRateUnits) {
            out.rate_unit = std::max(out.rate_unit, Measure(factory, net_format.Get(), typography.Get(), unit));
        }
    }
    out.rate = out.rate_prefix + out.rate_number + out.rate_space + out.rate_unit;

    if (style.variant == config::StripVariant::Stack) {
        // 百分比列按最宽的 “100%” 固定，名称更宽时跟着名称。温度同样按最宽的可能读数，避免数字一变整条就伸缩
        const float percent = Measure(factory, value_format.Get(), typography.Get(), L"100%");
        const float celsius = Measure(factory, value_format.Get(), typography.Get(), L"999°");
        for (int i = 0; i < kStripItemCount; ++i) {
            const config::StripSource source = style.items[i].source;
            if (source == config::StripSource::Empty || IsRate(source)) {
                continue;
            }
            const float label = Measure(factory, label_format.Get(), typography.Get(), labels ? labels[i] : nullptr);
            out.text[i] = std::max(label, source == config::StripSource::Temp ? celsius : percent);
        }
        return out;
    }

    // 样式 B / C（D-075）：名称和数值在同一行。B 的名称用标签字号，C 的名称和数值同一字号
    const bool spark = style.variant == config::StripVariant::Spark;
    IDWriteTextFormat* name_format = spark ? label_format.Get() : compact_label_format.Get();
    IDWriteTextFormat* number_format = compact_value_format.Get();
    const float percent = Measure(factory, number_format, typography.Get(), L"100%");
    const float celsius = Measure(factory, number_format, typography.Get(), L"999°");
    const auto short_widest = [&](config::NetUnit unit) {
        switch (unit) {
        case config::NetUnit::Bits:
            return Widest(factory, number_format, typography.Get(), {L"888Kb", L"8.8Mb", unavailable});
        case config::NetUnit::Kilo:
            return Widest(factory, number_format, typography.Get(), {L"88888K", unavailable});
        case config::NetUnit::Mega:
            return Widest(factory, number_format, typography.Get(), {L"8.88M", L"888M", unavailable});
        case config::NetUnit::Auto:
            break;
        }
        return Widest(factory, number_format, typography.Get(), {L"888K", L"8.8M", L"888B", unavailable});
    };
    const float net_short = short_widest(style.net_unit);
    const float disk_short = short_widest(config::NetUnit::Auto);
    out.compact_space = Measure(factory, name_format, typography.Get(), L" ");
    // 样式 B 的曲线至少这么宽（设计稿 52 px），名称和数值更宽时跟着变宽
    constexpr float kSparkMinWidth = 52.0f;
    const float name_gap = spark ? 4.0f : out.compact_space;
    for (int i = 0; i < kStripItemCount; ++i) {
        const config::StripSource source = style.items[i].source;
        if (source == config::StripSource::Empty) {
            continue;
        }
        float width = 0.0f;
        if (IsRate(source)) {
            const bool net = source == config::StripSource::Net;
            const float number = net ? net_short : disk_short;
            if (spark) {
                // B：网速只画下载，磁盘画读写之和，各带一条曲线
                out.label[i] = Measure(factory, name_format, typography.Get(), prefix(net ? 0 : 4));
                width = out.label[i] + name_gap + number;
            } else {
                // C：“↓2.4M ↑180K”，两半各占一样宽
                const float mark = std::max(Measure(factory, name_format, typography.Get(), prefix(net ? 0 : 2)),
                                            Measure(factory, name_format, typography.Get(), prefix(net ? 1 : 3)));
                out.compact_rate_part = std::max(out.compact_rate_part, mark + number);
                width = 2 * (mark + number) + out.compact_space;
            }
        } else {
            out.label[i] = Measure(factory, name_format, typography.Get(), labels ? labels[i] : nullptr);
            const float value = source == config::StripSource::Temp ? celsius : percent;
            width = out.label[i] + (out.label[i] > 0.0f ? name_gap : 0.0f) + value;
        }
        out.text[i] = spark ? std::max(width, kSparkMinWidth) : width;
    }
    return out;
}

StripMeasured MeasureStripStyle(IDWriteFactory* factory, const config::StripStyle& style) {
    const wchar_t* labels[kStripItemCount];
    for (int i = 0; i < kStripItemCount; ++i) {
        labels[i] = StripItemLabel(style.items[i]);
    }
    const Strings& s = Str();
    const wchar_t* prefixes[kRatePrefixCount] = {s.kArrowDown, s.kArrowUp, s.kDiskReadShort, s.kDiskWriteShort,
                                                 s.kLabelDisk};
    return MeasureStripColumns(factory, style, labels, prefixes, s.kUnavailable);
}

}  // namespace pitot::ui
