#pragma once

#include <functional>
#include <memory>
#include <unordered_map>

#include "lyric_data.h"

struct RubyTextMetrics
{
    float width;
    float ascent;
    float descent;
    std::shared_ptr<IUnknown> native_layout = {};
};

using RubyMeasureText = std::function<std::optional<RubyTextMetrics>(std::tstring_view, bool)>;

struct RubyRun
{
    std::tstring base;
    std::tstring reading;
    float x = 0;
    float width = 0;
    RubyTextMetrics base_metrics = {};
    RubyTextMetrics reading_metrics = {};
};

struct RubyRow
{
    std::vector<RubyRun> runs;
    float width = 0;
    float baseline = 0;
    float ruby_baseline = 0;
    float height = 0;
};

struct RubyLayout
{
    std::vector<RubyRow> rows;
    float height = 0;
    float first_baseline = 0;
    float first_ruby_band = 0;
};

std::optional<RubyLayout> build_ruby_layout(const LyricDataLine& line,
                                            float width,
                                            float linegap,
                                            float ruby_gap,
                                            const RubyMeasureText& measure);

struct RubyLayoutSettings
{
    LOGFONT font = {};
    float width = 0;
    float dpi = 96;
    int linegap = 0;
    TextAlignment alignment = TextAlignment::MidCentre;

    bool operator==(const RubyLayoutSettings& other) const;
};

// Cache per panel, not per HDC/device; colors and playback position are not layout inputs.
class RubyLayoutCache
{
public:
    const std::optional<RubyLayout>& get(const LyricDataLine& line,
                                         const RubyLayoutSettings& settings,
                                         const RubyMeasureText& measure);
    void clear();

private:
    struct Entry
    {
        LyricDataLine line;
        RubyLayoutSettings settings;
        std::optional<RubyLayout> layout;
    };
    std::unordered_map<const LyricDataLine*, Entry> m_entries;
};

float ruby_row_left(TextAlignment alignment, float available_width, float row_width);
const LyricDataLine& generated_reading_band_probe();
