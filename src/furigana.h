#pragma once

#include "lyric_data.h"

namespace furigana
{
    struct BasePosition
    {
        size_t start;
        size_t length;
    };

    bool is_kana_tag(std::string_view line);
    size_t cluster_end(std::tstring_view text, size_t start);
    std::vector<BasePosition> base_positions(std::tstring_view text);
    void decode(LyricData& lyrics); // Before equal-time lines are combined.
    void remember_source_lines(LyricData& lyrics);
    std::vector<std::string> tags_for_save(const LyricData& lyrics);
    std::vector<LyricDataLine> split_lines(const LyricData& lyrics);

    // Preserve only associations which can be identified unambiguously.
    void preserve_annotations(const LyricData& before, LyricData& after, bool nonbase_edit = false);
}
