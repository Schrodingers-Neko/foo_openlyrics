#pragma once

#include "lyric_data.h"
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>

namespace furigana_generation
{
    struct ReadingToken
    {
        size_t start;
        size_t length;
        std::tstring reading;
        bool known;
    };

    class ReadingAnalyzer
    {
    public:
        virtual ~ReadingAnalyzer() = default;
        virtual std::vector<ReadingToken> analyze(std::tstring_view text) = 0;
    };

    std::unique_ptr<ReadingAnalyzer> create_analyzer(const std::tstring& directory);
    bool automatic_eligible(std::tstring_view text);
    bool eligible(const LyricData& lyrics, bool force_japanese);
    std::vector<FuriganaSpan> annotate(std::tstring_view text, const std::vector<ReadingToken>& tokens);
    std::tstring hiragana(std::tstring_view reading);

    class AnnotationCache
    {
    public:
        const std::vector<FuriganaSpan>& get(const std::tstring& text,
                                             const std::function<std::vector<FuriganaSpan>()>& create);
        void clear();
        size_t bytes() const
        {
            return m_bytes;
        }

    private:
        struct Entry
        {
            std::vector<FuriganaSpan> spans;
            std::list<std::tstring>::iterator age;
            size_t bytes;
        };
        std::unordered_map<std::tstring, Entry> m_entries;
        std::list<std::tstring> m_age;
        size_t m_bytes = 0;
        std::vector<FuriganaSpan> m_uncached;
    };

    // Pure generation; never changes lyric text, timing, metadata, or save state.
    std::vector<std::vector<FuriganaSpan>> generate(ReadingAnalyzer& analyzer,
                                                    const LyricData& lyrics,
                                                    bool force_japanese,
                                                    const std::function<bool()>& cancelled,
                                                    AnnotationCache* cache = nullptr);
}
