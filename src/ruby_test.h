#pragma once

#include "furigana_generator.h"

// Native offscreen tests may export their actual renderer output for visual inspection.
// The output directory is supplied by the test invocation, never by lyric metadata.
inline bool save_ruby_test_bitmap(const TCHAR* name, int width, int height, UINT stride, const uint8_t* pixels)
{
    TCHAR directory[32768];
    const DWORD count = GetEnvironmentVariable(_T("OPENLYRICS_FURIGANA_RENDER_DIR"),
                                               directory,
                                               DWORD(std::size(directory)));
    if(count == 0) return true;
    if(count >= std::size(directory)) return false;
    const std::tstring path = std::tstring(directory) + _T("\\") + name;
    HANDLE file = CreateFile(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(file == INVALID_HANDLE_VALUE) return false;
    BITMAPFILEHEADER file_header = {};
    file_header.bfType = 0x4d42;
    file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file_header.bfSize = file_header.bfOffBits + DWORD(width * height * 4);
    BITMAPINFOHEADER bitmap = {};
    bitmap.biSize = sizeof(bitmap);
    bitmap.biWidth = width;
    bitmap.biHeight = -height;
    bitmap.biPlanes = 1;
    bitmap.biBitCount = 32;
    bitmap.biCompression = BI_RGB;
    DWORD written;
    bool success = WriteFile(file, &file_header, sizeof(file_header), &written, nullptr) != FALSE;
    success &= WriteFile(file, &bitmap, sizeof(bitmap), &written, nullptr) != FALSE;
    for(int row = 0; row < height; ++row)
        success &= WriteFile(file, pixels + size_t(row) * stride, DWORD(width * 4), &written, nullptr) != FALSE;
    CloseHandle(file);
    return success;
}

inline LyricDataLine ruby_test_line()
{
    return { _T("今日の日々 Hello\n覚醒 覚ませ"),
             1.0,
             { { 0, 2, _T("きょう") },
               { 3, 1, _T("ひ") },
               { 4, 1, _T("び") },
               { 12, 1, _T("かく") },
               { 13, 1, _T("せい") },
               { 15, 1, _T("さ") } } };
}

inline LyricDataLine generated_ruby_test_line()
{
    LyricDataLine line { _T("今日の大人が食べる\n取り戻す歌"), 1.0 };
    TCHAR dictionary[32768];
    if(!GetEnvironmentVariable(_T("OPENLYRICS_GENERATION_DICTIONARY"), dictionary, DWORD(std::size(dictionary))))
        return ruby_test_line();
    auto analyzer = furigana_generation::create_analyzer(dictionary);
    LyricData lyrics {};
    lyrics.lines.push_back(line);
    line.furigana = furigana_generation::generate(*analyzer, lyrics, false, [] { return false; })[0];
    return line;
}
