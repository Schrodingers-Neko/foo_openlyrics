#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <span>
#include <stdint.h>
#include <string>
#include <vector>

namespace furigana_dictionary
{
    inline constexpr char version[] = "ipadic-utf8-20070801-v1";
    inline constexpr char download_url[] = "https://github.com/Schrodingers-Neko/foo_openlyrics/releases/download/"
                                           "furigana-dictionary-ipadic-20070801-v1/"
                                           "openlyrics-ipadic-utf8-20070801-v1.zip";
    inline constexpr char archive_hash[] = "3df6f40dfb4d7ddb558299ec4f39dc79ca32cd5ace076eb7bf61fe86f3d49412";
    inline constexpr size_t archive_size = 13399728;
    inline constexpr uint64_t installed_size = 52936022;
    struct FileSpec
    {
        const char* name;
        uint64_t size;
        const char* sha256;
    };
    inline constexpr std::array<FileSpec, 8> files = {
        { { "char.bin", 262496, "81bba502ae48fa005a374819f15e44452eb68086a8b323ec20a044d514400832" },
          { "matrix.bin", 3463716, "ee44d7350cdcb680ebd699f83e121be1dc63310f8832d55bcb537a068177611a" },
          { "sys.dic", 49199027, "47ba5ff7f00328a04bde39fa679943c4a87f863dfef0cf1cf4d006dbf2134ab9" },
          { "unk.dic", 5690, "a55cbb28acef6c8b51757e176b7c158540649b6b4d9e5849785b44596017f51f" },
          { "dicrc", 79, "1264d62ee2654e5690818dd3409fce0c01e5bb82583cb51ac9b2e887f1764383" },
          { "mecabrc", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
          { "COPYING", 3797, "69a739afe63b22464f2122aa55498ac54e8eb49cfff6df50ce62ac5154f8277d" },
          { "manifest.json", 1217, "1e62b2ab4858006481e1c0d65e431fb61cadb00e74f88c26e3f5e67d812d6e8f" } }
    };
    using Cancelled = std::function<bool()>;
    using Progress = std::function<void(uint64_t, uint64_t)>;
    std::string sha256(std::span<const uint8_t> data);
    std::vector<uint8_t> download(const Cancelled& cancelled, const Progress& progress);
    // Public validation seam for malformed/untrusted archive tests; installation additionally pins the full digest.
    void validate_archive(std::span<const uint8_t> archive);
    class Store
    {
    public:
        explicit Store(std::filesystem::path root);
        std::filesystem::path directory() const;
        bool present() const;
        void verify(const Cancelled& cancelled) const;
        void install(std::span<const uint8_t> archive, const Cancelled& cancelled);
        void remove();

    private:
        std::filesystem::path m_root;
        void check_path(const std::filesystem::path& path) const;
        void erase_owned(const std::filesystem::path& path) const;
    };
}
