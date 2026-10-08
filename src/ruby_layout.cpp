#include "stdafx.h"

#include "furigana.h"
#include "mvtf/mvtf.h"
#include "ruby_layout.h"

static bool is_cjk(TCHAR c)
{
    return (c >= 0x3000 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0xff00 && c <= 0xffef);
}

const LyricDataLine& generated_reading_band_probe()
{
    // Reserve the native reading font's height while background generation is pending.
    static const LyricDataLine probe { _T("日"), 0.0, { { 0, 1, _T("あ") } } };
    return probe;
}

std::optional<RubyLayout> build_ruby_layout(const LyricDataLine& line,
                                            float width,
                                            float linegap,
                                            float ruby_gap,
                                            const RubyMeasureText& measure)
{
    const auto font = measure(_T("Mg"), false);
    if(!font) return {};
    width = std::max(0.0f, width);
    RubyLayout result;
    RubyRow row;
    const auto finish_row = [&]()
    {
        while(!row.runs.empty() && row.runs.back().reading.empty()
              && find_first_nonwhitespace(row.runs.back().base) == std::tstring::npos)
        {
            row.width -= row.runs.back().width;
            row.runs.pop_back();
        }
        float ascent = font->ascent;
        float descent = font->descent;
        float ruby_ascent = 0;
        float ruby_descent = 0;
        for(const auto& run : row.runs)
        {
            ascent = std::max(ascent, run.base_metrics.ascent);
            descent = std::max(descent, run.base_metrics.descent);
            ruby_ascent = std::max(ruby_ascent, run.reading_metrics.ascent);
            ruby_descent = std::max(ruby_descent, run.reading_metrics.descent);
        }
        const float band = ruby_ascent > 0 ? ruby_ascent + ruby_descent + ruby_gap : 0;
        row.baseline = result.height + band + ascent;
        row.ruby_baseline = result.height + ruby_ascent;
        row.height = band + ascent + descent + linegap;
        if(result.rows.empty())
        {
            result.first_baseline = row.baseline;
            result.first_ruby_band = band;
        }
        result.height += row.height;
        result.rows.push_back(std::move(row));
        row = {};
    };
    size_t pos = 0;
    while(pos < line.text.size())
    {
        if(line.text[pos] == _T('\n'))
        {
            finish_row();
            ++pos;
            continue;
        }
        RubyRun run;
        const auto span = std::find_if(line.furigana.begin(),
                                       line.furigana.end(),
                                       [&](const auto& value) { return value.start == pos; });
        size_t end;
        if(span != line.furigana.end())
        {
            if(span->length == 0 || span->length > line.text.size() - pos) return {};
            end = pos + span->length;
            run.reading = span->reading;
        }
        else
        {
            end = furigana::cluster_end(line.text, pos);
            if(!is_cjk(line.text[pos]) && !is_char_whitespace(line.text[pos]))
            {
                while(end < line.text.size() && !is_cjk(line.text[end]) && !is_char_whitespace(line.text[end]))
                {
                    if(std::any_of(line.furigana.begin(),
                                   line.furigana.end(),
                                   [&](const auto& value) { return value.start == end; }))
                        break;
                    end = furigana::cluster_end(line.text, end);
                }
            }
        }
        run.base = line.text.substr(pos, end - pos);
        pos = end;
        const auto base_metrics = measure(run.base, false);
        if(!base_metrics) return {};
        run.base_metrics = *base_metrics;
        if(!run.reading.empty())
        {
            const auto reading_metrics = measure(run.reading, true);
            if(!reading_metrics) return {};
            run.reading_metrics = *reading_metrics;
        }
        run.width = std::max(run.base_metrics.width, run.reading_metrics.width);
        const bool whitespace = run.reading.empty() && find_first_nonwhitespace(run.base) == std::tstring::npos;
        if(!row.runs.empty() && row.width + run.width > width) finish_row();
        if(whitespace && row.runs.empty()) continue;
        run.x = row.width;
        row.width += run.width;
        row.runs.push_back(std::move(run));
    }
    finish_row();
    return result;
}

bool RubyLayoutSettings::operator==(const RubyLayoutSettings& other) const
{
    return width == other.width && dpi == other.dpi && linegap == other.linegap && alignment == other.alignment
           && memcmp(&font, &other.font, sizeof(font)) == 0;
}

const std::optional<RubyLayout>& RubyLayoutCache::get(const LyricDataLine& line,
                                                      const RubyLayoutSettings& settings,
                                                      const RubyMeasureText& measure)
{
    if(m_entries.size() > 2048) clear();
    const auto existing = m_entries.find(&line);
    if(existing != m_entries.end() && existing->second.settings == settings && existing->second.line.text == line.text
       && existing->second.line.furigana == line.furigana)
        return existing->second.layout;
    Entry entry {
        line,
        settings,
        build_ruby_layout(line, settings.width, float(settings.linegap), 2.0f * settings.dpi / 96.0f, measure)
    };
    return m_entries.insert_or_assign(&line, std::move(entry)).first->second.layout;
}

void RubyLayoutCache::clear()
{
    m_entries.clear();
}

float ruby_row_left(TextAlignment alignment, float available_width, float row_width)
{
    if(row_width > available_width) return 0;
    switch(alignment)
    {
        case TextAlignment::MidCentre:
        case TextAlignment::TopCentre: return (available_width - row_width) * 0.5f;
        case TextAlignment::MidRight:
        case TextAlignment::TopRight: return available_width - row_width;
        default: return 0;
    }
}

#if MVTF_TESTS_ENABLED
static std::optional<RubyTextMetrics> test_measure(std::tstring_view text, bool ruby)
{
    return RubyTextMetrics { float(text.size()) * (ruby ? 5.0f : 10.0f), ruby ? 4.0f : 8.0f, ruby ? 1.0f : 2.0f };
}

MVTF_TEST(ruby_layout_keeps_groups_together_and_reserves_actual_height)
{
    const LyricDataLine line { _T("今日明日"), 1.0, { { 0, 2, _T("きょう") }, { 2, 2, _T("あした") } } };
    const auto layout = build_ruby_layout(line, 25, 3, 2, test_measure);
    ASSERT(layout && layout->rows.size() == 2);
    ASSERT(layout->rows[0].runs.size() == 1 && layout->rows[1].runs.size() == 1);
    ASSERT(layout->height == 40 && layout->first_ruby_band == 7);
    const auto narrow = build_ruby_layout(line, 1, 3, 2, test_measure);
    ASSERT(narrow && narrow->rows.size() == 2);
    ASSERT(ruby_row_left(TextAlignment::MidCentre, 1, 20) == 0);
}

MVTF_TEST(ruby_layout_compound_blank_and_plain_rows)
{
    const LyricDataLine line { _T("日\n\nHello"), 1.0, { { 0, 1, _T("ひ") } } };
    const auto layout = build_ruby_layout(line, 100, 3, 2, test_measure);
    ASSERT(layout && layout->rows.size() == 3);
    ASSERT(layout->rows[0].height == 20 && layout->rows[1].height == 13 && layout->rows[2].height == 13);
    ASSERT(layout->height == 46);
    ASSERT(ruby_row_left(TextAlignment::MidCentre, 100, 20) == 40);
    ASSERT(ruby_row_left(TextAlignment::TopRight, 100, 20) == 80);
}

MVTF_TEST(ruby_layout_does_not_split_utf16_clusters)
{
    const LyricDataLine line { _T("日😀か\u3099"), 1.0, { { 0, 1, _T("ひ") } } };
    const auto layout = build_ruby_layout(line, 1, 0, 2, test_measure);
    ASSERT(layout && layout->rows.size() == 3);
    ASSERT(layout->rows[1].runs[0].base == _T("😀"));
    ASSERT(layout->rows[2].runs[0].base == _T("か\u3099"));
}

MVTF_TEST(ruby_layout_cache_reuses_geometry_and_invalidates_style_and_text)
{
    RubyLayoutCache cache;
    RubyLayoutSettings settings;
    settings.width = 100;
    LyricDataLine line { _T("日"), 1.0, { { 0, 1, _T("ひ") } } };
    int calls = 0;
    const auto measure = [&](std::tstring_view text, bool ruby)
    {
        ++calls;
        return test_measure(text, ruby);
    };
    ASSERT(cache.get(line, settings, measure).has_value());
    const int initial = calls;
    line.timestamp = 10; // Playback/timing does not affect geometry.
    ASSERT(cache.get(line, settings, measure).has_value() && calls == initial);
    settings.dpi = 144;
    ASSERT(cache.get(line, settings, measure).has_value() && calls > initial);
    const int after_dpi = calls;
    line.furigana[0].reading = _T("にち");
    ASSERT(cache.get(line, settings, measure).has_value() && calls > after_dpi);
    ASSERT(!build_ruby_layout(line,
                              100,
                              0,
                              2,
                              [](std::tstring_view, bool) -> std::optional<RubyTextMetrics> { return {}; }));
}
#endif
