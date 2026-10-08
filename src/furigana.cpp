#include "stdafx.h"

#include "furigana.h"
#include "logging.h"
#include "mvtf/mvtf.h"
#include "parsers.h"

bool furigana::is_kana_tag(std::string_view line)
{
    constexpr std::string_view prefix = "[kana:";
    if(line.size() < prefix.size()) return false;
    for(size_t i = 0; i < prefix.size(); ++i)
    {
        char c = line[i];
        if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if(c != prefix[i]) return false;
    }
    return true;
}

static bool is_digit(TCHAR c)
{
    return (c >= _T('0') && c <= _T('9')) || (c >= 0xff10 && c <= 0xff19);
}

size_t furigana::cluster_end(std::tstring_view text, size_t start)
{
    size_t end = start + 1;
    if(text[start] >= 0xd800 && text[start] <= 0xdbff && end < text.size() && text[end] >= 0xdc00
       && text[end] <= 0xdfff)
        ++end;
    while(end < text.size())
    {
        WORD type = 0;
        GetStringTypeW(CT_CTYPE3, &text[end], 1, &type);
        // C3_DIACRITIC also marks precomposed letters (for example べ), not just combining marks.
        if((type & (C3_NONSPACING | C3_VOWELMARK)) != 0 || (text[end] >= 0xfe00 && text[end] <= 0xfe0f))
            ++end;
        else if(text[end] == 0xdb40 && end + 1 < text.size() && text[end + 1] >= 0xdd00 && text[end + 1] <= 0xddef)
            end += 2;
        else
            break;
    }
    return end;
}

std::vector<furigana::BasePosition> furigana::base_positions(std::tstring_view text)
{
    std::vector<BasePosition> result;
    for(size_t i = 0; i < text.size(); ++i)
    {
        const TCHAR c = text[i];
        if(is_digit(c))
        {
            const size_t start = i;
            while(i + 1 < text.size() && is_digit(text[i + 1]))
                ++i;
            result.push_back({ start, i - start + 1 });
        }
        else if((c >= 0x3400 && c <= 0x4dbf) || (c >= 0x4e00 && c <= 0x9fff) || c == 0x3005 || c == 0x3006)
        {
            const size_t end = cluster_end(text, i);
            result.push_back({ i, end - i });
            i = end - 1;
        }
    }
    return result;
}

struct KanaUnit
{
    size_t count;
    std::tstring reading;
};

static std::optional<std::vector<KanaUnit>> parse_units(std::string_view tag)
{
    if(tag.size() <= 6 || tag.back() != ']') return {};
    const std::tstring payload = to_tstring(tag.substr(6, tag.size() - 7));
    std::vector<KanaUnit> units;
    size_t pos = 0;
    while(pos < payload.size())
    {
        if(payload[pos] < _T('1') || payload[pos] > _T('9')) return {};
        const size_t count = size_t(payload[pos++] - _T('0'));
        const size_t start = pos;
        while(pos < payload.size() && !(payload[pos] >= _T('1') && payload[pos] <= _T('9')))
        {
            // Timed kana and malformed delimiters are deliberately unsupported.
            const TCHAR c = payload[pos++];
            if(c == _T('0') || c == _T('(') || c == _T(')') || c == _T('[') || c == _T(']') || c == _T('\r')
               || c == _T('\n'))
                return {};
        }
        units.push_back({ count, payload.substr(start, pos - start) });
    }
    return units;
}

void furigana::decode(LyricData& lyrics)
{
    if(!lyrics.has_kana_metadata) return;
    bool valid = lyrics.raw_kana_tags.size() == 1;
    const auto units = valid ? parse_units(lyrics.raw_kana_tags[0]) : std::nullopt;
    valid = valid && units.has_value();
    struct Location
    {
        size_t line;
        BasePosition position;
    };
    std::vector<Location> locations;
    for(size_t i = 0; i < lyrics.lines.size(); ++i)
    {
        for(const auto& position : base_positions(lyrics.lines[i].text))
            locations.push_back({ i, position });
    }
    size_t cursor = 0;
    if(valid)
    {
        for(const auto& unit : *units)
        {
            if(unit.count > locations.size() - cursor)
            {
                valid = false;
                break;
            }
            const auto& first = locations[cursor];
            const auto& last = locations[cursor + unit.count - 1];
            if(!unit.reading.empty())
            {
                bool contiguous = first.line == last.line;
                for(size_t i = cursor + 1; contiguous && i < cursor + unit.count; ++i)
                {
                    const auto& previous = locations[i - 1].position;
                    contiguous = locations[i].position.start == previous.start + previous.length;
                }
                if(!contiguous)
                {
                    valid = false;
                    break;
                }
                const size_t length = last.position.start + last.position.length - first.position.start;
                lyrics.lines[first.line].furigana.push_back({ first.position.start, length, unit.reading });
            }
            cursor += unit.count;
        }
        valid = valid && cursor == locations.size();
    }
    lyrics.kana_metadata_valid = valid;
    if(!valid)
    {
        for(auto& line : lyrics.lines)
            line.furigana.clear();
        LOG_WARN("Ignoring unsupported or misaligned kana metadata");
    }
}

static std::vector<std::tstring> ordered_bodies(const LyricData& lyrics)
{
    auto lines = furigana::split_lines(lyrics);
    std::stable_sort(lines.begin(),
                     lines.end(),
                     [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
    std::vector<std::tstring> result;
    for(auto& line : lines)
        result.push_back(std::move(line.text));
    return result;
}

void furigana::remember_source_lines(LyricData& lyrics)
{
    lyrics.kana_source_lines = ordered_bodies(lyrics);
}

std::vector<LyricDataLine> furigana::split_lines(const LyricData& lyrics)
{
    std::vector<LyricDataLine> result;
    for(const auto& line : lyrics.lines)
    {
        size_t start = 0;
        do
        {
            const size_t end = std::min(line.text.size(), line.text.find(_T('\n'), start));
            LyricDataLine part { line.text.substr(start, end - start), line.timestamp };
            for(const auto& span : line.furigana)
            {
                if(span.start >= start && span.start + span.length <= end)
                    part.furigana.push_back({ span.start - start, span.length, span.reading });
            }
            result.push_back(std::move(part));
            start = end + 1;
        } while(start <= line.text.size());
    }
    return result;
}

std::vector<std::string> furigana::tags_for_save(const LyricData& lyrics)
{
    if(!lyrics.has_kana_metadata) return {};
    if(!lyrics.kana_metadata_valid)
    {
        const auto current = ordered_bodies(lyrics);
        if(current == lyrics.kana_source_lines) return lyrics.raw_kana_tags;
        LOG_WARN("Dropping stale unsupported kana metadata after lyric edits");
        return {};
    }
    std::tstring payload;
    auto lines = split_lines(lyrics);
    std::stable_sort(lines.begin(),
                     lines.end(),
                     [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
    for(const auto& line : lines)
    {
        const auto positions = base_positions(line.text);
        for(size_t i = 0; i < positions.size();)
        {
            const auto found = std::find_if(line.furigana.begin(),
                                            line.furigana.end(),
                                            [&](const auto& span) { return span.start == positions[i].start; });
            size_t count = 1;
            if(found != line.furigana.end())
            {
                const size_t end = found->start + found->length;
                while(i + count < positions.size() && positions[i + count].start < end)
                    ++count;
                if(count > 9 || positions[i + count - 1].start + positions[i + count - 1].length != end)
                {
                    LOG_WARN("Cannot serialize invalid furigana span");
                    return {};
                }
            }
            payload += TCHAR(_T('0') + count);
            if(found != line.furigana.end()) payload += found->reading;
            i += count;
        }
    }
    return { "[kana:" + from_tstring(payload) + "]" };
}

static bool remap_spans(const LyricDataLine& before, LyricDataLine& after)
{
    const auto old_positions = furigana::base_positions(before.text);
    const auto new_positions = furigana::base_positions(after.text);
    if(old_positions.size() != new_positions.size()) return false;
    for(size_t i = 0; i < old_positions.size(); ++i)
    {
        if(before.text.substr(old_positions[i].start, old_positions[i].length)
           != after.text.substr(new_positions[i].start, new_positions[i].length))
            return false;
    }
    std::vector<FuriganaSpan> spans;
    for(const auto& span : before.furigana)
    {
        size_t first = 0;
        while(first < old_positions.size() && old_positions[first].start != span.start)
            ++first;
        if(first == old_positions.size()) return false;
        size_t last = first;
        while(last + 1 < old_positions.size() && old_positions[last + 1].start < span.start + span.length)
            ++last;
        for(size_t i = first + 1; i <= last; ++i)
            if(new_positions[i].start != new_positions[i - 1].start + new_positions[i - 1].length) return false;
        spans.push_back({ new_positions[first].start,
                          new_positions[last].start + new_positions[last].length - new_positions[first].start,
                          span.reading });
    }
    after.furigana = std::move(spans);
    return true;
}

void furigana::preserve_annotations(const LyricData& before, LyricData& after, bool nonbase_edit)
{
    if(!before.has_kana_metadata) return;
    after.has_kana_metadata = true;
    after.kana_metadata_valid = before.kana_metadata_valid;
    after.raw_kana_tags = before.raw_kana_tags;
    after.kana_source_lines = before.kana_source_lines;
    if(!before.kana_metadata_valid) return;

    // Physical line occurrence matters when only timestamps changed, including duplicate text.
    auto old_lines = split_lines(before);
    auto new_lines = split_lines(after);
    bool same_bodies = old_lines.size() == new_lines.size();
    for(size_t i = 0; same_bodies && i < old_lines.size(); ++i)
        same_bodies = old_lines[i].text == new_lines[i].text;
    for(size_t i = 0; i < new_lines.size(); ++i)
    {
        auto& line = new_lines[i];
        line.furigana.clear();
        if(same_bodies || (nonbase_edit && old_lines.size() == new_lines.size()))
        {
            remap_spans(old_lines[i], line);
            continue;
        }
        std::vector<const LyricDataLine*> candidates;
        for(const auto& old : old_lines)
            if(old.text == line.text && old.timestamp == line.timestamp) candidates.push_back(&old);
        if(candidates.empty())
            for(const auto& old : old_lines)
                if(old.text == line.text) candidates.push_back(&old);
        if(candidates.empty()) continue;
        bool equivalent = true;
        for(const auto* old : candidates)
            equivalent &= old->furigana == candidates[0]->furigana;
        if(equivalent) remap_spans(*candidates[0], line);
    }
    // Return annotations to compound lines without changing their text or timestamps.
    size_t index = 0;
    for(auto& line : after.lines)
    {
        line.furigana.clear();
        size_t start = 0;
        do
        {
            for(const auto& span : new_lines[index++].furigana)
                line.furigana.push_back({ span.start + start, span.length, span.reading });
            start = std::min(line.text.size(), line.text.find(_T('\n'), start)) + 1;
        } while(start <= line.text.size());
    }
}

#if MVTF_TESTS_ENABLED
static bool equivalent_lyrics(const LyricData& a, const LyricData& b)
{
    if(a.lines.size() != b.lines.size() || a.timestamp_offset != b.timestamp_offset)
    {
        printf("Round-trip line counts: %zu / %zu\n", a.lines.size(), b.lines.size());
        return false;
    }
    for(size_t i = 0; i < a.lines.size(); ++i)
        if(a.lines[i].text != b.lines[i].text || a.lines[i].timestamp != b.lines[i].timestamp
           || a.lines[i].furigana != b.lines[i].furigana)
        {
            printf("Round-trip mismatch at line %zu: text=%d, time=%d, furigana=%d\n",
                   i,
                   a.lines[i].text != b.lines[i].text,
                   a.lines[i].timestamp != b.lines[i].timestamp,
                   a.lines[i].furigana != b.lines[i].furigana);
            return false;
        }
    return true;
}

MVTF_TEST(furigana_footer_repeated_timestamps_and_occurrence_readings)
{
    const auto data = parsers::lrc::parse({}, "[00:03.00]覚\n[00:01.00][00:02.00]覚\n[kana:1かく1かく1さ]");
    ASSERT(data.kana_metadata_valid);
    ASSERT(data.lines.size() == 3);
    ASSERT(data.lines[0].furigana[0].reading == _T("かく"));
    ASSERT(data.lines[2].furigana[0].reading == _T("さ"));
    for(bool merge : { false, true })
    {
        const auto reparsed = parsers::lrc::parse({}, from_tstring(parsers::lrc::expand_text(data, merge)));
        ASSERT(equivalent_lyrics(data, reparsed));
    }
}

MVTF_TEST(furigana_grouped_numeric_iteration_and_empty_entries)
{
    const auto data = parsers::lrc::parse({}, "[KANA:2きょう1111び1しめ]\n[00:01.00]今日22/７日々〆");
    ASSERT(data.kana_metadata_valid);
    ASSERT(data.lines[0].furigana.size() == 3);
    ASSERT(data.lines[0].furigana[0] == (FuriganaSpan { 0, 2, _T("きょう") }));
    ASSERT(data.lines[0].furigana[1] == (FuriganaSpan { 7, 1, _T("び") }));
    ASSERT(data.lines[0].furigana[2].reading == _T("しめ"));
    const auto reparsed = parsers::lrc::parse({}, from_tstring(parsers::lrc::expand_text(data, true)));
    ASSERT(equivalent_lyrics(data, reparsed));
}

MVTF_TEST(furigana_equal_time_compound_and_unsynced_roundtrip)
{
    auto data = parsers::lrc::parse({}, "[00:01.00]日\n[00:01.00]々\n[kana:1ひ1び]");
    ASSERT(data.lines.size() == 1);
    ASSERT(data.lines[0].furigana[1].start == 2);
    ASSERT(equivalent_lyrics(data, parsers::lrc::parse({}, from_tstring(parsers::lrc::expand_text(data, true)))));
    data.RemoveTimestamps();
    const auto unsynced = parsers::lrc::parse({}, from_tstring(parsers::lrc::expand_text(data, true)));
    ASSERT(unsynced.lines.size() == 2);
    ASSERT(unsynced.lines[1].furigana[0].reading == _T("び"));
}

MVTF_TEST(furigana_invalid_metadata_is_atomic_hidden_and_preserved)
{
    for(const std::string& tag : { "[kana:1に]",
                                   "[kana:1に1ち1よ]",
                                   "[kana:0に1ち]",
                                   "[kana:1に(100,20)1ち]",
                                   "[kana:2にち",
                                   "[kana:2にち]\n[kana:2にち]" })
    {
        const auto data = parsers::lrc::parse({}, "[00:01.00]日地\n" + tag);
        ASSERT(data.lines.size() == 1);
        ASSERT(!data.kana_metadata_valid);
        ASSERT(data.lines[0].furigana.empty());
        for(const auto& raw : data.raw_kana_tags)
            ASSERT(parsers::lrc::expand_text(data, false).find(to_tstring(raw)) != std::tstring::npos);
        auto changed = data;
        changed.lines[0].text = _T("今日");
        ASSERT(parsers::lrc::expand_text(changed, false).find(_T("[kana:")) == std::tstring::npos);
    }
    const auto cross_line = parsers::lrc::parse({}, "[kana:2にち]\n[00:01.00]日\n[00:02.00]地");
    ASSERT(!cross_line.kana_metadata_valid);
    const auto partial = parsers::lrc::parse({}, "[00:01.00]日地\n[kana:1に]");
    ASSERT(partial.lines[0].furigana.empty());
    auto reordered = parsers::lrc::parse({}, "[00:01.00]日\n[00:02.00]地\n[kana:1に(100,20)1ち]");
    reordered.lines[0].timestamp = 3;
    ASSERT(parsers::lrc::expand_text(reordered, false).find(_T("[kana:")) == std::tstring::npos);
}

MVTF_TEST(furigana_output_purposes_do_not_leak_metadata)
{
    const auto data = parsers::lrc::parse({}, "[ti:example]\n[00:01.00]日\n[kana:1ひ]");
    ASSERT(parsers::lrc::expand_text(data, false).find(_T("[kana:1ひ]")) != std::tstring::npos);
    for(auto purpose : { parsers::lrc::TextPurpose::Editor, parsers::lrc::TextPurpose::Upload })
    {
        const auto text = parsers::lrc::expand_text(data, false, purpose);
        ASSERT(text.find(_T("[kana:")) == std::tstring::npos);
        ASSERT(text.find(_T("日")) != std::tstring::npos);
        ASSERT(text.find(_T("[ti:example]")) != std::tstring::npos);
    }
}

MVTF_TEST(furigana_editor_matching_preserves_only_known_associations)
{
    const auto before = parsers::lrc::parse({}, "[00:01.00]覚\n[00:02.00]日\n[00:03.00]覚\n[kana:1かく1ひ1さ]");
    auto retimed = parsers::lrc::parse({}, "[00:04.00]覚\n[00:05.00]日\n[00:06.00]覚");
    furigana::preserve_annotations(before, retimed);
    ASSERT(retimed.lines[0].furigana[0].reading == _T("かく"));
    ASSERT(retimed.lines[2].furigana[0].reading == _T("さ"));
    auto edited = parsers::lrc::parse({}, "[00:08.00]覚\n[00:09.00]日\n[00:10.00]地");
    furigana::preserve_annotations(before, edited);
    ASSERT(edited.lines[0].furigana.empty()); // Duplicate text, differing readings.
    ASSERT(edited.lines[1].furigana[0].reading == _T("ひ"));
    ASSERT(edited.lines[2].furigana.empty()); // Same length is not enough.
    auto reordered = parsers::lrc::parse({}, "[00:00.50]日\n[00:03.00]覚");
    furigana::preserve_annotations(before, reordered);
    ASSERT(reordered.lines[0].furigana[0].reading == _T("ひ"));
    ASSERT(reordered.lines[1].furigana[0].reading == _T("さ"));
}

MVTF_TEST(furigana_nonbase_edits_remap_utf16_offsets)
{
    const auto before = parsers::lrc::parse({}, "[00:01.00] A  日 \n[kana:1ひ]");
    auto after = parsers::lrc::parse({}, "[00:01.00]a 日");
    furigana::preserve_annotations(before, after, true);
    ASSERT(after.lines[0].furigana[0].start == 2);
    ASSERT(after.lines[0].furigana[0].reading == _T("ひ"));
    const auto unicode = parsers::lrc::parse({}, "[00:01.00]😀𠮷日\n[kana:1ひ]");
    ASSERT(unicode.kana_metadata_valid);
    ASSERT(unicode.lines[0].furigana[0].start == 4);
    const auto variation = parsers::lrc::parse({}, "[00:01.00]日\uFE00\n[kana:1ひ]");
    ASSERT(variation.kana_metadata_valid);
    ASSERT(variation.lines[0].furigana[0].length == 2);
    ASSERT(equivalent_lyrics(variation,
                             parsers::lrc::parse({}, from_tstring(parsers::lrc::expand_text(variation, false)))));
}

MVTF_TEST(furigana_editor_retiming_duplicate_bodies_keeps_physical_occurrences)
{
    const auto before = parsers::lrc::parse({}, "[00:01.00]覚\n[00:02.00]覚\n[kana:1かく1さ]");
    const auto after = parsers::lrc::parse_editor(before, "[00:03.00]覚\n[00:01.00]覚");
    ASSERT(after.lines[0].furigana[0].reading == _T("さ"));
    ASSERT(after.lines[1].furigana[0].reading == _T("かく"));
    ASSERT(equivalent_lyrics(after, parsers::lrc::parse({}, from_tstring(parsers::lrc::expand_text(after, true)))));
    const auto physical_snapshot = parsers::lrc::parse_editor(before, "[00:03.00]覚\n[00:01.00]覚", false);
    const auto reapplied = parsers::lrc::parse_editor(physical_snapshot, "[00:04.00]覚\n[00:01.00]覚");
    ASSERT(reapplied.lines[0].furigana[0].reading == _T("さ"));
    ASSERT(reapplied.lines[1].furigana[0].reading == _T("かく"));
    const auto imported = parsers::lrc::parse_editor(before, "[00:03.00]日\n[00:01.00]覚\n[kana:1さ1ひ]");
    ASSERT(imported.kana_metadata_valid);
    ASSERT(imported.lines[0].furigana[0].reading == _T("さ"));
    ASSERT(imported.lines[1].furigana[0].reading == _T("ひ"));
}

MVTF_TEST(furigana_supplied_fixture_when_requested)
{
    TCHAR path[32768];
    const DWORD count = GetEnvironmentVariable(_T("OPENLYRICS_FURIGANA_FIXTURE"), path, DWORD(std::size(path)));
    if(count == 0) return; // The copyrighted fixture remains outside repository tests.
    ASSERT(count < std::size(path));
    HANDLE file =
        CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT(file != INVALID_HANDLE_VALUE);
    const DWORD size = GetFileSize(file, nullptr);
    std::string contents(size, '\0');
    DWORD read = 0;
    const bool success = ReadFile(file, contents.data(), size, &read, nullptr) != FALSE;
    CloseHandle(file);
    ASSERT(success && read == size);
    const auto data = parsers::lrc::parse({}, contents);
    printf("Fixture read: %lu bytes, %zu lines, kana valid=%d\n", read, data.lines.size(), data.kana_metadata_valid);
    ASSERT(data.kana_metadata_valid);
    const auto lines = furigana::split_lines(data);
    const size_t timed_lines = size_t(
        std::count_if(lines.begin(), lines.end(), [](const auto& line) { return line.timestamp != DBL_MAX; }));
    printf("Fixture timed lines: %zu\n", timed_lines);
    ASSERT(timed_lines == 49);
    size_t positions = 0;
    size_t readings = 0;
    for(const auto& line : lines)
    {
        positions += furigana::base_positions(line.text).size();
        readings += line.furigana.size();
    }
    printf("Fixture positions: %zu, readings: %zu\n", positions, readings);
    ASSERT(positions == 157 && readings == 101);
    for(bool merge : { false, true })
        ASSERT(equivalent_lyrics(data, parsers::lrc::parse({}, from_tstring(parsers::lrc::expand_text(data, merge)))));
    printf("Supplied furigana fixture: 49 lines, 157 positions, 101 readings verified\n");
}
#endif
