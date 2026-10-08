#include "stdafx.h"

#include "furigana.h"
#include "furigana_generator.h"
#include "mvtf/mvtf.h"
#include "parsers.h"

#pragma warning(push, 0)
#include <mecab.h>
#pragma warning(pop)
#include <fstream>
#if MVTF_TESTS_ENABLED
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif
#include <stdexcept>
#include <unordered_map>

namespace
{
    uint32_t codepoint(std::tstring_view text, size_t pos)
    {
        uint32_t c = uint16_t(text[pos]);
        if(c >= 0xd800 && c <= 0xdbff && pos + 1 < text.size())
        {
            const uint32_t low = uint16_t(text[pos + 1]);
            if(low >= 0xdc00 && low <= 0xdfff) c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00;
        }
        return c;
    }

    bool han(uint32_t c)
    {
        return (c >= 0x3400 && c <= 0x4dbf) || (c >= 0x4e00 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff)
               || (c >= 0x20000 && c <= 0x323af) || c == 0x3005 || c == 0x3006;
    }

    bool kana(uint32_t c)
    {
        return (c >= 0x3041 && c <= 0x3096) || (c >= 0x30a1 && c <= 0x30fa) || (c >= 0xff66 && c <= 0xff9d);
    }

    std::tstring normalize(std::tstring_view input)
    {
        if(input.empty()) return {};
        const int size = NormalizeString(NormalizationKC, input.data(), int(input.size()), nullptr, 0);
        if(size <= 0) return {};
        std::tstring result(size_t(size), 0);
        const int written = NormalizeString(NormalizationKC, input.data(), int(input.size()), result.data(), size);
        if(written <= 0) return {};
        result.resize(size_t(written));
        return result;
    }

    bool valid_reading(std::tstring_view text)
    {
        return !text.empty() && text.size() <= 256
               && std::all_of(text.begin(),
                              text.end(),
                              [](TCHAR c)
                              {
                                  return (c >= 0x3041 && c <= 0x3096) || c == 0x3099 || c == 0x309a || c == 0x309d
                                         || c == 0x309e || c == 0x30fc;
                              });
    }

    struct Segment
    {
        size_t start;
        size_t length;
        bool is_han;
        std::tstring anchor;
    };

    std::vector<FuriganaSpan> align(std::tstring_view surface, const std::tstring& reading, size_t offset)
    {
        std::vector<Segment> segments;
        for(size_t pos = 0; pos < surface.size();)
        {
            const bool is_han = han(codepoint(surface, pos));
            if(!is_han && !kana(codepoint(surface, pos))) return {};
            const size_t start = pos;
            do
            {
                pos = furigana::cluster_end(surface, pos);
            } while(pos < surface.size() && (is_han ? han(codepoint(surface, pos)) : kana(codepoint(surface, pos))));
            const auto body = surface.substr(start, pos - start);
            segments.push_back(
                { start, pos - start, is_han, is_han ? std::tstring() : furigana_generation::hiragana(body) });
        }
        if(segments.empty() || segments.size() > 32) return {};
        std::vector<size_t> boundaries { 0 };
        for(size_t pos = 0; pos < reading.size();)
        {
            pos = furigana::cluster_end(reading, pos);
            boundaries.push_back(pos);
        }
        const size_t stride = reading.size() + 1;
        std::vector<uint8_t> ways((segments.size() + 1) * stride, 0);
        ways[segments.size() * stride + reading.size()] = 1;
        for(size_t s = segments.size(); s-- > 0;)
        {
            for(const size_t pos : boundaries)
            {
                unsigned count = 0;
                if(segments[s].is_han)
                {
                    for(const size_t end : boundaries)
                        if(end > pos) count = std::min(2U, count + ways[(s + 1) * stride + end]);
                }
                else if(reading.compare(pos, segments[s].anchor.size(), segments[s].anchor) == 0)
                    count = ways[(s + 1) * stride + pos + segments[s].anchor.size()];
                ways[s * stride + pos] = uint8_t(count);
            }
        }
        if(ways[0] != 1) return { { offset, surface.size(), reading } };
        std::vector<FuriganaSpan> result;
        size_t pos = 0;
        for(size_t s = 0; s < segments.size(); ++s)
        {
            const auto& segment = segments[s];
            if(segment.is_han)
            {
                for(const size_t end : boundaries)
                {
                    if(end > pos && ways[(s + 1) * stride + end] != 0)
                    {
                        result.push_back({ offset + segment.start, segment.length, reading.substr(pos, end - pos) });
                        pos = end;
                        break;
                    }
                }
            }
            else
                pos += segment.anchor.size();
        }
        return result;
    }

    class MecabAnalyzer final : public furigana_generation::ReadingAnalyzer
    {
    public:
        explicit MecabAnalyzer(const std::tstring& directory)
        {
            const std::string dir = from_tstring(directory);
            const std::string rc = from_tstring(directory + _T("\\mecabrc"));
            const char* argv[] = { "openlyrics", "-r", rc.c_str(), "-d", dir.c_str() };
            m_tagger.reset(MeCab::createTagger(5, const_cast<char**>(argv)));
            if(!m_tagger) throw std::runtime_error("The reading dictionary could not be opened");
            const auto* info = m_tagger->dictionary_info();
            if(!info || std::string_view(info->charset) != "UTF-8" || info->version != 102)
                throw std::runtime_error("The reading dictionary format is incompatible");
        }

        std::vector<furigana_generation::ReadingToken> analyze(std::tstring_view text) override
        {
            if(text.size() > 4096) return {};
            // Only actual UTF-8 boundaries can map to a UTF-16 range.
            std::string input;
            std::vector<size_t> offsets { 0 };
            for(size_t pos = 0; pos < text.size();)
            {
                const uint32_t c = codepoint(text, pos);
                if(c >= 0xd800 && c <= 0xdfff) return {};
                const size_t next = pos + (c > 0xffff ? 2 : 1);
                const auto bytes = from_tstring(text.substr(pos, next - pos));
                if(bytes.empty()) return {};
                input += bytes;
                offsets.resize(input.size() + 1, SIZE_MAX);
                offsets[input.size()] = next;
                pos = next;
            }
            const auto* node = m_tagger->parseToNode(input.c_str(), input.size());
            if(!node) throw std::runtime_error("Japanese text analysis failed");
            std::vector<furigana_generation::ReadingToken> result;
            for(; node; node = node->next)
            {
                if(node->stat != MECAB_NOR_NODE && node->stat != MECAB_UNK_NODE) continue;
                const ptrdiff_t begin = node->surface - input.data();
                if(begin < 0 || size_t(begin) > input.size() || node->length > input.size() - size_t(begin)) continue;
                const size_t end = size_t(begin) + node->length;
                if(offsets[size_t(begin)] == SIZE_MAX || offsets[end] == SIZE_MAX) continue;
                const size_t start = offsets[size_t(begin)], length = offsets[end] - start;
                if(from_tstring(text.substr(start, length)) != std::string_view(node->surface, node->length)) continue;
                std::string_view feature(node->feature);
                for(int i = 0; i < 7; ++i)
                {
                    const size_t comma = feature.find(',');
                    if(comma == feature.npos)
                    {
                        feature = {};
                        break;
                    }
                    feature.remove_prefix(comma + 1);
                }
                feature = feature.substr(0, feature.find(','));
                if(feature == "*") feature = {};
                result.push_back({ start, length, to_tstring(feature), node->stat == MECAB_NOR_NODE });
            }
            return result;
        }

    private:
        std::unique_ptr<MeCab::Tagger> m_tagger;
    };
}

std::unique_ptr<furigana_generation::ReadingAnalyzer> furigana_generation::create_analyzer(
    const std::tstring& directory)
{
    return std::make_unique<MecabAnalyzer>(directory);
}

std::tstring furigana_generation::hiragana(std::tstring_view reading)
{
    const auto normalized = normalize(reading);
    std::tstring result;
    for(const auto c : normalized)
    {
        if(c >= 0x30a1 && c <= 0x30f6)
            result += TCHAR(c - 0x60);
        else if(c == 0x30fd || c == 0x30fe)
            result += TCHAR(c - 0x60);
        else if(c >= 0x30f7 && c <= 0x30fa)
        {
            constexpr TCHAR bases[] = { 0x308f, 0x3090, 0x3091, 0x3092 };
            result += bases[c - 0x30f7];
            result += TCHAR(0x3099);
        }
        else
            result += c;
    }
    return result;
}

bool furigana_generation::automatic_eligible(std::tstring_view text)
{
    bool has_han = false, has_kana = false;
    for(size_t pos = 0; pos < text.size(); pos = furigana::cluster_end(text, pos))
    {
        const auto c = codepoint(text, pos);
        has_han |= han(c);
        has_kana |= kana(c);
    }
    return has_han && has_kana;
}

bool furigana_generation::eligible(const LyricData& lyrics, bool force_japanese)
{
    if(lyrics.has_kana_metadata) return false;
    size_t total = 0;
    bool candidate = false;
    for(const auto& line : lyrics.lines)
    {
        if(line.text.size() > 65536 - total) return false;
        total += line.text.size();
        for(size_t start = 0; start < line.text.size();)
        {
            const size_t end = std::min(line.text.size(), line.text.find(_T('\n'), start));
            const auto body = std::tstring_view(line.text).substr(start, end - start);
            if(body.size() <= 4096)
            {
                if(!force_japanese)
                    candidate |= automatic_eligible(body);
                else
                    for(size_t pos = 0; pos < body.size(); pos = furigana::cluster_end(body, pos))
                        candidate |= han(codepoint(body, pos));
            }
            start = end + 1;
        }
    }
    return candidate;
}

std::vector<FuriganaSpan> furigana_generation::annotate(std::tstring_view text, const std::vector<ReadingToken>& tokens)
{
    std::vector<FuriganaSpan> result;
    size_t previous_end = 0;
    for(const auto& token : tokens)
    {
        if(!token.known || token.length == 0 || token.start < previous_end || token.start > text.size()
           || token.length > text.size() - token.start || token.length > 256)
            continue;
        const auto surface = text.substr(token.start, token.length);
        const auto reading = hiragana(token.reading);
        bool has_han = false;
        for(size_t pos = 0; pos < surface.size(); pos = furigana::cluster_end(surface, pos))
            has_han |= han(codepoint(surface, pos));
        if(!has_han || !valid_reading(reading)) continue;
        bool boundaries = false;
        for(size_t pos = 0; pos < text.size(); pos = furigana::cluster_end(text, pos))
            if(pos == token.start)
            {
                boundaries = true;
                break;
            }
        if(!boundaries || furigana::cluster_end(text, token.start + token.length - 1) != token.start + token.length)
            continue;
        auto spans = align(surface, reading, token.start);
        result.insert(result.end(), std::make_move_iterator(spans.begin()), std::make_move_iterator(spans.end()));
        previous_end = token.start + token.length;
    }
    return result;
}

std::vector<std::vector<FuriganaSpan>> furigana_generation::generate(ReadingAnalyzer& analyzer,
                                                                     const LyricData& lyrics,
                                                                     bool force_japanese,
                                                                     const std::function<bool()>& cancelled,
                                                                     AnnotationCache* cache)
{
    std::vector<std::vector<FuriganaSpan>> result(lyrics.lines.size());
    if(lyrics.has_kana_metadata) return result;
    size_t total = 0;
    for(const auto& line : lyrics.lines)
        total += line.text.size();
    if(total > 65536) return result;
    std::unordered_map<std::tstring, std::vector<FuriganaSpan>> repeated;
    for(size_t i = 0; i < lyrics.lines.size(); ++i)
    {
        const auto& text = lyrics.lines[i].text;
        size_t start = 0;
        while(start < text.size())
        {
            if(cancelled()) throw std::runtime_error("Generation cancelled");
            const size_t end = std::min(text.size(), text.find(_T('\n'), start));
            const auto body = text.substr(start, end - start);
            if(body.size() <= 4096 && (force_japanese || automatic_eligible(body)))
            {
                const auto found = repeated.find(body);
                const auto create = [&] { return annotate(body, analyzer.analyze(body)); };
                const auto spans = found != repeated.end() ? found->second
                                   : cache                 ? cache->get(body, create)
                                                           : create();
                repeated.insert_or_assign(body, spans);
                for(const auto& span : spans)
                    result[i].push_back({ span.start + start, span.length, span.reading });
            }
            start = end + 1;
        }
    }
    return result;
}

const std::vector<FuriganaSpan>& furigana_generation::AnnotationCache::get(
    const std::tstring& text,
    const std::function<std::vector<FuriganaSpan>()>& create)
{
    const auto found = m_entries.find(text);
    if(found != m_entries.end())
    {
        m_age.splice(m_age.end(), m_age, found->second.age);
        return found->second.spans;
    }
    auto spans = create();
    size_t bytes = 2 * (text.capacity() + 1) * sizeof(TCHAR) + sizeof(Entry) + sizeof(std::tstring) * 2 + 64
                   + spans.capacity() * sizeof(FuriganaSpan);
    for(const auto& span : spans)
        bytes += (span.reading.capacity() + 1) * sizeof(TCHAR);
    constexpr size_t budget = 8 * 1024 * 1024;
    if(bytes > budget)
    {
        m_uncached = std::move(spans);
        return m_uncached;
    }
    while(!m_age.empty() && (m_bytes + bytes > budget || m_entries.size() >= 1024))
    {
        const auto old = m_entries.find(m_age.front());
        m_bytes -= old->second.bytes;
        m_entries.erase(old);
        m_age.pop_front();
    }
    m_age.push_back(text);
    m_bytes += bytes;
    return m_entries.emplace(text, Entry { std::move(spans), std::prev(m_age.end()), bytes }).first->second.spans;
}

void furigana_generation::AnnotationCache::clear()
{
    m_entries.clear();
    m_age.clear();
    m_bytes = 0;
    m_uncached.clear();
}

#if MVTF_TESTS_ENABLED
#define GEN_ASSERT(condition)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if(!(condition)) printf("Generation assertion at line %d: %s\n", __LINE__, #condition);                        \
        ASSERT(condition);                                                                                             \
    } while(false)
MVTF_TEST(generation_alignment_okurigana_compounds_and_ambiguity)
{
    using namespace furigana_generation;
    GEN_ASSERT(annotate(_T("食べる"), { { 0, 3, _T("タベル"), true } })
               == (std::vector<FuriganaSpan> { { 0, 1, _T("た") } }));
    GEN_ASSERT(annotate(_T("取り戻す"), { { 0, 4, _T("トリモドス"), true } })
               == (std::vector<FuriganaSpan> { { 0, 1, _T("と") }, { 2, 1, _T("もど") } }));
    GEN_ASSERT(annotate(_T("大人"), { { 0, 2, _T("オトナ"), true } })
               == (std::vector<FuriganaSpan> { { 0, 2, _T("おとな") } }));
    GEN_ASSERT(annotate(_T("甲あ乙"), { { 0, 3, _T("アアアア"), true } })
               == (std::vector<FuriganaSpan> { { 0, 3, _T("ああああ") } }));
    GEN_ASSERT(annotate(_T("3人"), { { 0, 2, _T("サンニン"), true } }).empty());
    GEN_ASSERT(annotate(_T("日"), { { 0, 1, _T("ニチ"), false } }).empty());
    GEN_ASSERT(annotate(_T("日"), { { 0, 1, _T("*"), true } }).empty());
    GEN_ASSERT(hiragana(_T("ｶﾞッコー")) == _T("がっこー"));
    GEN_ASSERT(automatic_eligible(_T("今日の空")));
    GEN_ASSERT(!automatic_eligible(_T("中国文字")) && !automatic_eligible(_T("かなだけ")));
}

MVTF_TEST(generation_unicode_offsets_and_metadata_exclusion)
{
    using namespace furigana_generation;
    GEN_ASSERT(annotate(_T("😀𠮷野"), { { 2, 3, _T("ヨシノ"), true } })
               == (std::vector<FuriganaSpan> { { 2, 3, _T("よしの") } }));
    GEN_ASSERT(annotate(_T("日\ufe00"), { { 0, 2, _T("ヒ"), true } })
               == (std::vector<FuriganaSpan> { { 0, 2, _T("ひ") } }));
    GEN_ASSERT(annotate(_T("日\ufe00"), { { 0, 1, _T("ヒ"), true } }).empty());
    struct Fake final : ReadingAnalyzer
    {
        int calls = 0;
        std::vector<ReadingToken> analyze(std::tstring_view) override
        {
            ++calls;
            return { { 0, 1, _T("ヒ"), true } };
        }
    } analyzer;
    LyricData data {};
    data.lines = { { _T("日の歌\n日の歌"), 1.0 }, { _T("日の歌"), 3.0 } };
    const auto result = generate(analyzer, data, false, [] { return false; });
    GEN_ASSERT(analyzer.calls == 1 && result[0].size() == 2 && result[0][1].start == 4 && result[1].size() == 1);
    data.has_kana_metadata = true;
    GEN_ASSERT(generate(analyzer, data, true, [] { return false; })[0].empty() && analyzer.calls == 1);
}

MVTF_TEST(generation_eligibility_before_dictionary_loading)
{
    using namespace furigana_generation;
    LyricData data {};
    data.lines = { { _T("English words\n中国文字\nかなだけ"), 0.0 } };
    GEN_ASSERT(!eligible(data, false));
    GEN_ASSERT(eligible(data, true));
    data.lines = { { _T("今日の空"), 0.0 } };
    GEN_ASSERT(eligible(data, false));
    data.has_kana_metadata = true;
    GEN_ASSERT(!eligible(data, true));
    data.has_kana_metadata = false;
    data.lines = { { std::tstring(4097, _T('日')) + _T("の"), 0.0 } };
    GEN_ASSERT(!eligible(data, false) && !eligible(data, true));
    data.lines = { { std::tstring(65537, _T('日')), 0.0 }, { _T("今日の空"), 1.0 } };
    GEN_ASSERT(!eligible(data, false));
}

MVTF_TEST(generation_native_dictionary_when_requested)
{
    TCHAR path[32768];
    if(!GetEnvironmentVariable(_T("OPENLYRICS_GENERATION_DICTIONARY"), path, DWORD(std::size(path)))) return;
    const auto start = std::chrono::steady_clock::now();
    auto analyzer = furigana_generation::create_analyzer(path);
    const auto initialized = std::chrono::steady_clock::now();
    const std::tstring body = _T("😀 今日の日々を取り戻す。食べる大人。");
    const auto spans = furigana_generation::annotate(body, analyzer->analyze(body));
    GEN_ASSERT(std::find(spans.begin(), spans.end(), FuriganaSpan { 3, 2, _T("きょう") }) != spans.end());
    GEN_ASSERT(std::find(spans.begin(), spans.end(), FuriganaSpan { 6, 2, _T("ひび") }) != spans.end());
    GEN_ASSERT(std::find(spans.begin(), spans.end(), FuriganaSpan { 14, 1, _T("た") }) != spans.end());
    GEN_ASSERT(std::find(spans.begin(), spans.end(), FuriganaSpan { 17, 2, _T("おとな") }) != spans.end());
    const auto done = std::chrono::steady_clock::now();
    printf("Native dictionary: init %.2fms, analysis %.2fms, %zu spans\n",
           std::chrono::duration<double, std::milli>(initialized - start).count(),
           std::chrono::duration<double, std::milli>(done - initialized).count(),
           spans.size());
}

MVTF_TEST(generation_cache_reuses_annotations_and_bounds_memory)
{
    using namespace furigana_generation;
    AnnotationCache cache;
    int calls = 0;
    const auto create = [&]
    {
        ++calls;
        return std::vector<FuriganaSpan> { { 0, 1, _T("ひ") } };
    };
    ASSERT(cache.get(_T("日"), create).size() == 1);
    ASSERT(cache.get(_T("日"), create).size() == 1 && calls == 1);
    for(int i = 0; i < 1500; ++i)
        cache.get(std::format(_T("日{}"), i), create);
    ASSERT(cache.bytes() <= 8 * 1024 * 1024);
    cache.get(_T("日"), create);
    ASSERT(calls == 1502);
    cache.clear();
    ASSERT(cache.bytes() == 0);
}

MVTF_TEST(generation_supplied_song_quality_and_source_preservation_when_requested)
{
    using namespace furigana_generation;
    TCHAR dictionary[32768], fixture[32768];
    if(!GetEnvironmentVariable(_T("OPENLYRICS_GENERATION_DICTIONARY"), dictionary, DWORD(std::size(dictionary)))
       || !GetEnvironmentVariable(_T("OPENLYRICS_FURIGANA_FIXTURE"), fixture, DWORD(std::size(fixture))))
        return;
    std::ifstream input(fixture, std::ios::binary);
    const std::string original((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto embedded = parsers::lrc::parse({}, original);
    auto source = embedded;
    source.has_kana_metadata = false;
    source.kana_metadata_valid = false;
    source.raw_kana_tags.clear();
    source.kana_source_lines.clear();
    for(auto& line : source.lines)
        line.furigana.clear();
    const auto before = parsers::lrc::expand_text(source, false);
    PROCESS_MEMORY_COUNTERS_EX memory_before {};
    memory_before.cb = sizeof(memory_before);
    GetProcessMemoryInfo(GetCurrentProcess(),
                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory_before),
                         sizeof(memory_before));
    auto analyzer = create_analyzer(dictionary);
    AnnotationCache cache;
    const auto start = std::chrono::steady_clock::now();
    const auto result = generate(*analyzer, source, false, [] { return false; }, &cache);
    const auto generated = std::chrono::steady_clock::now();
    ASSERT(generate(*analyzer, source, false, [] { return false; }, &cache) == result);
    const auto cached = std::chrono::steady_clock::now();
    PROCESS_MEMORY_COUNTERS_EX memory_after {};
    memory_after.cb = sizeof(memory_after);
    GetProcessMemoryInfo(GetCurrentProcess(),
                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory_after),
                         sizeof(memory_after));
    size_t spans = 0, comparable = 0, matched = 0;
    for(size_t i = 0; i < result.size(); ++i)
    {
        size_t end = 0;
        for(const auto& span : result[i])
        {
            ASSERT(span.start >= end && span.length > 0 && span.start + span.length <= source.lines[i].text.size());
            end = span.start + span.length;
            ++spans;
            std::tstring supplied;
            size_t cursor = span.start;
            for(const auto& gold : embedded.lines[i].furigana)
            {
                if(gold.start == cursor && gold.start + gold.length <= end)
                {
                    supplied += gold.reading;
                    cursor += gold.length;
                }
            }
            if(cursor == end)
            {
                ++comparable;
                matched += supplied == span.reading;
            }
        }
    }
    ASSERT(spans > 20 && before == parsers::lrc::expand_text(source, false));
    for(const auto& line : source.lines)
        ASSERT(line.furigana.empty());
    printf("Supplied song generation: %zu spans, %zu/%zu comparable embedded readings agree; warm %.2fms, cached "
           "%.2fms, cache %zu bytes\n",
           spans,
           matched,
           comparable,
           std::chrono::duration<double, std::milli>(generated - start).count(),
           std::chrono::duration<double, std::milli>(cached - generated).count(),
           cache.bytes());
    printf("Dictionary/generation observed memory delta: working set %+.2f MiB, private %+.2f MiB (Debug, warm OS file "
           "cache)\n",
           (double(memory_after.WorkingSetSize) - double(memory_before.WorkingSetSize)) / 1048576.0,
           (double(memory_after.PrivateUsage) - double(memory_before.PrivateUsage)) / 1048576.0);
}
#endif
