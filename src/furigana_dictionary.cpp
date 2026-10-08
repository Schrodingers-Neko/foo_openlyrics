#include "stdafx.h"

#include "furigana_dictionary.h"
#include "furigana_generator.h"
#include "hash_utils.h"
#include "mvtf/mvtf.h"
#include "openlyrics_version.h"

#define CURL_STATICLIB
#pragma warning(push, 0)
#include <curl/curl.h>
#include <miniz.h>
#pragma warning(pop)
#include <atomic>
#include <fstream>
#include <set>
#include <stdexcept>

namespace
{
    using namespace furigana_dictionary;
    void require(bool condition, const char* message)
    {
        if(!condition) throw std::runtime_error(message);
    }
    void check_cancel(const Cancelled& cancelled)
    {
        require(!cancelled(), "Dictionary operation cancelled");
    }
    const FileSpec* file_spec(std::string_view name)
    {
        const auto found = std::find_if(files.begin(),
                                        files.end(),
                                        [&](const auto& file) { return name == file.name; });
        return found == files.end() ? nullptr : &*found;
    }
    std::string finish_hash(Sha256Context& hash)
    {
        uint8_t bytes[32];
        hash.finalise(bytes);
        require(!hash.m_error, "Dictionary checksum calculation failed");
        std::string result;
        constexpr char digits[] = "0123456789abcdef";
        for(uint8_t byte : bytes)
        {
            result += digits[byte >> 4];
            result += digits[byte & 15];
        }
        return result;
    }
    struct Archive
    {
        mz_zip_archive zip {};
        explicit Archive(std::span<const uint8_t> bytes)
        {
            require(bytes.size() <= 16 * 1024 * 1024, "Dictionary archive exceeds the download limit");
            require(mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0) != 0, "Dictionary archive is invalid");
        }
        ~Archive()
        {
            mz_zip_reader_end(&zip);
        }
        Archive(const Archive&) = delete;
    };
    void validate(Archive& archive)
    {
        require(mz_zip_reader_get_num_files(&archive.zip) == files.size(), "Dictionary archive has unexpected files");
        std::set<std::string> seen;
        uint64_t total = 0;
        for(mz_uint i = 0; i < files.size(); ++i)
        {
            mz_zip_archive_file_stat stat {};
            require(mz_zip_reader_file_stat(&archive.zip, i, &stat) != 0, "Dictionary archive directory is invalid");
            const auto* spec = file_spec(stat.m_filename);
            const auto mode = (stat.m_external_attr >> 16) & 0170000;
            require(spec && !stat.m_is_directory && !stat.m_is_encrypted && stat.m_is_supported
                        && (mode == 0 || mode == 0100000) && (stat.m_external_attr & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
                    "Dictionary archive contains an unsafe or unexpected entry");
            require(seen.insert(stat.m_filename).second, "Dictionary archive contains duplicate entries");
            require(stat.m_uncomp_size == spec->size, "Dictionary archive contains an incorrect file size");
            total += stat.m_uncomp_size;
        }
        require(total == installed_size, "Dictionary archive installed size is incorrect");
    }
    struct Extraction
    {
        HANDLE file;
        Sha256Context hash;
        const Cancelled& cancelled;
        uint64_t written = 0;
        uint64_t maximum;
        bool failed = false;
    };
    size_t extract(void* opaque, mz_uint64 offset, const void* bytes, size_t count)
    {
        auto& state = *static_cast<Extraction*>(opaque);
        try
        {
            if(state.cancelled() || offset != state.written || count > state.maximum - state.written)
            {
                state.failed = true;
                return 0;
            }
            DWORD written = 0;
            if(!WriteFile(state.file, bytes, DWORD(count), &written, nullptr) || written != count)
            {
                state.failed = true;
                return 0;
            }
            state.hash.add_data(static_cast<const uint8_t*>(bytes), count);
            state.written += count;
            return count;
        }
        catch(...)
        {
            state.failed = true;
            return 0;
        }
    }
    struct Download
    {
        std::vector<uint8_t> bytes;
        const Cancelled& cancelled;
        const Progress& progress;
    };
    size_t receive(char* data, size_t size, size_t count, void* opaque)
    {
        auto& state = *static_cast<Download*>(opaque);
        try
        {
            if(size != 1 || count > archive_size - state.bytes.size() || state.cancelled()) return 0;
            state.bytes.insert(state.bytes.end(),
                               reinterpret_cast<uint8_t*>(data),
                               reinterpret_cast<uint8_t*>(data) + count);
        }
        catch(...)
        {
            return 0;
        }
        return count;
    }
    int progress(void* opaque, curl_off_t, curl_off_t current, curl_off_t, curl_off_t)
    {
        auto& state = *static_cast<Download*>(opaque);
        try
        {
            if(state.cancelled()) return 1;
            state.progress(uint64_t(std::max<curl_off_t>(0, current)), archive_size);
            return 0;
        }
        catch(...)
        {
            return 1;
        }
    }
}

std::string furigana_dictionary::sha256(std::span<const uint8_t> data)
{
    Sha256Context hash;
    hash.add_data(data.data(), data.size());
    return finish_hash(hash);
}

std::vector<uint8_t> furigana_dictionary::download(const Cancelled& cancelled, const Progress& report)
{
    check_cancel(cancelled);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    require(curl != nullptr, "Could not start the dictionary download");
    Download state { {}, cancelled, report };
    state.bytes.reserve(archive_size);
    curl_easy_setopt(curl.get(), CURLOPT_URL, download_url);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "foo_openlyrics/" OPENLYRICS_VERSION);
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 180L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXFILESIZE_LARGE, curl_off_t(archive_size));
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &state);
    const auto status = curl_easy_perform(curl.get());
    check_cancel(cancelled);
    require(status == CURLE_OK, "Dictionary download failed. Check the network and retry.");
    long response = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response);
    require(response == 200 && state.bytes.size() == archive_size,
            "The dictionary download is incomplete or unavailable");
    require(sha256(state.bytes) == archive_hash, "Dictionary download checksum mismatch");
    return std::move(state.bytes);
}

void furigana_dictionary::validate_archive(std::span<const uint8_t> bytes)
{
    Archive archive(bytes);
    validate(archive);
}

furigana_dictionary::Store::Store(std::filesystem::path root)
    : m_root(std::filesystem::absolute(std::move(root)).lexically_normal())
{
}
std::filesystem::path furigana_dictionary::Store::directory() const
{
    return m_root / L"dictionaries" / version;
}

void furigana_dictionary::Store::check_path(const std::filesystem::path& path) const
{
    const auto relative = path.lexically_normal().lexically_relative(m_root);
    require(!relative.empty() && !relative.is_absolute(), "Dictionary path is outside its managed directory");
    for(const auto& part : relative)
        require(part != L"..", "Dictionary path escapes its managed directory");
    auto check = m_root;
    const auto check_entry = [](const auto& entry)
    {
        const DWORD attributes = GetFileAttributesW(entry.c_str());
        require(attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
                "Dictionary storage contains a link or reparse point");
    };
    check_entry(check);
    for(const auto& part : relative)
    {
        check /= part;
        check_entry(check);
    }
}

bool furigana_dictionary::Store::present() const
{
    try
    {
        check_path(directory());
        for(const auto& file : files)
        {
            const auto path = directory() / file.name;
            check_path(path);
            if(!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != file.size) return false;
        }
        return true;
    }
    catch(...)
    {
        return false;
    }
}

void furigana_dictionary::Store::verify(const Cancelled& cancelled) const
{
    require(present(), "The reading dictionary is missing or incomplete. Download it again to repair it.");
    std::array<uint8_t, 65536> buffer;
    for(const auto& file : files)
    {
        check_cancel(cancelled);
        std::ifstream input(directory() / file.name, std::ios::binary);
        require(bool(input), "The reading dictionary cannot be read");
        Sha256Context hash;
        while(input)
        {
            check_cancel(cancelled);
            input.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(buffer.size()));
            hash.add_data(buffer.data(), size_t(input.gcount()));
        }
        require(input.eof() && finish_hash(hash) == file.sha256,
                "The reading dictionary is damaged. Download it again to repair it.");
    }
}

void furigana_dictionary::Store::erase_owned(const std::filesystem::path& path) const
{
    check_path(path);
    if(!std::filesystem::exists(path)) return;
    for(const auto& entry : std::filesystem::directory_iterator(path))
    {
        check_path(entry.path());
        require(file_spec(entry.path().filename().string()) && entry.is_regular_file(),
                "Dictionary folder contains unrecognized files; removal stopped");
    }
    for(const auto& file : files)
        std::filesystem::remove(path / file.name);
    std::filesystem::remove(path);
}

void furigana_dictionary::Store::install(std::span<const uint8_t> bytes, const Cancelled& cancelled)
{
    check_cancel(cancelled);
    require(bytes.size() == archive_size && sha256(bytes) == archive_hash, "Dictionary archive checksum mismatch");
    Archive archive(bytes);
    validate(archive);
    check_path(m_root / L"dictionaries");
    std::filesystem::create_directories(m_root / L"dictionaries");
    ULARGE_INTEGER available {};
    require(GetDiskFreeSpaceExW(m_root.c_str(), &available, nullptr, nullptr) != 0
                && available.QuadPart >= installed_size + 1024 * 1024,
            "Not enough free space to install the reading dictionary");
    static std::atomic<uint64_t> sequence { 0 };
    const auto stage = m_root / std::format(L".staging-{}-{}-{}", GetCurrentProcessId(), GetTickCount64(), ++sequence);
    const auto backup = m_root / std::format(L".backup-{}-{}-{}", GetCurrentProcessId(), GetTickCount64(), ++sequence);
    require(std::filesystem::create_directory(stage), "Could not create dictionary staging directory");
    try
    {
        for(mz_uint i = 0; i < files.size(); ++i)
        {
            check_cancel(cancelled);
            mz_zip_archive_file_stat stat {};
            mz_zip_reader_file_stat(&archive.zip, i, &stat);
            const auto* spec = file_spec(stat.m_filename);
            const auto path = stage / spec->name;
            check_path(path);
            const HANDLE handle =
                CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            require(handle != INVALID_HANDLE_VALUE, "Could not write dictionary files");
            Extraction state { handle, {}, cancelled, 0, spec->size };
            const bool extracted = mz_zip_reader_extract_to_callback(&archive.zip, i, extract, &state, 0) != 0;
            const bool flushed = FlushFileBuffers(handle) != 0;
            CloseHandle(handle);
            check_cancel(cancelled);
            require(extracted && !state.failed && flushed && state.written == spec->size
                        && finish_hash(state.hash) == spec->sha256,
                    "Dictionary extraction or validation failed");
        }
        auto analyzer = furigana_generation::create_analyzer(stage.native());
        const auto tokens = analyzer->analyze(_T("今日の日々"));
        require(!tokens.empty(), "The installed dictionary failed its compatibility check");
        analyzer.reset();
        check_cancel(cancelled);
        bool backed_up = false;
        if(std::filesystem::exists(directory()))
        {
            // Check ownership before moving a directory, as well as before deleting it.
            for(const auto& entry : std::filesystem::directory_iterator(directory()))
            {
                check_path(entry.path());
                require(file_spec(entry.path().filename().string()) && entry.is_regular_file(),
                        "Existing dictionary folder contains unrecognized files");
            }
            std::filesystem::rename(directory(), backup);
            backed_up = true;
        }
        try
        {
            std::filesystem::rename(stage, directory());
        }
        catch(...)
        {
            if(backed_up) std::filesystem::rename(backup, directory());
            throw;
        }
        if(backed_up) erase_owned(backup);
    }
    catch(...)
    {
        try
        {
            erase_owned(stage);
        }
        catch(...)
        {
        }
        throw;
    }
}

void furigana_dictionary::Store::remove()
{
    erase_owned(directory());
}

#if MVTF_TESTS_ENABLED
MVTF_TEST(dictionary_download_only_when_explicitly_requested)
{
    TCHAR requested[4];
    if(!GetEnvironmentVariable(_T("OPENLYRICS_DICTIONARY_TEST_DOWNLOAD"), requested, DWORD(std::size(requested)))
       || std::tstring_view(requested) != _T("1"))
        return;
    ASSERT(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    struct Cleanup
    {
        ~Cleanup()
        {
            curl_global_cleanup();
        }
    } cleanup;
    uint64_t last_progress = 0;
    const auto downloaded = furigana_dictionary::download([] { return false; },
                                                          [&](uint64_t received, uint64_t expected)
                                                          {
                                                              last_progress = received;
                                                              if(expected != furigana_dictionary::archive_size)
                                                                  throw std::runtime_error("Unexpected download size");
                                                          });
    ASSERT(last_progress == furigana_dictionary::archive_size);
    ASSERT(furigana_dictionary::sha256(downloaded) == furigana_dictionary::archive_hash);
    furigana_dictionary::validate_archive(downloaded);
}

MVTF_TEST(dictionary_pinned_install_verify_remove_when_requested)
{
    TCHAR archive_path[32768], root_path[32768];
    if(!GetEnvironmentVariable(_T("OPENLYRICS_DICTIONARY_ARCHIVE"), archive_path, DWORD(std::size(archive_path)))
       || !GetEnvironmentVariable(_T("OPENLYRICS_DICTIONARY_TEST_ROOT"), root_path, DWORD(std::size(root_path))))
        return;
    std::ifstream input(archive_path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    furigana_dictionary::Store store(root_path);
    const bool existed = std::filesystem::exists(root_path);
    if(existed)
    {
        printf("Dictionary test root must be a new directory\n");
        ASSERT(false);
    }
    ASSERT(!store.present() && !std::filesystem::exists(root_path));
    bool rejected = false;
    try
    {
        store.install(bytes, [] { return true; });
    }
    catch(const std::exception&)
    {
        rejected = true;
    }
    ASSERT(rejected && !std::filesystem::exists(root_path));
    int cancellation_checks = 0;
    rejected = false;
    try
    {
        store.install(bytes, [&] { return ++cancellation_checks >= 10; });
    }
    catch(const std::exception&)
    {
        rejected = true;
    }
    ASSERT(rejected && !store.present());
    for(const auto& entry : std::filesystem::directory_iterator(root_path))
        ASSERT(entry.path().filename() == L"dictionaries");
    store.install(bytes, [] { return false; });
    ASSERT(store.present());
    store.verify([] { return false; });
    auto analyzer = furigana_generation::create_analyzer(store.directory().native());
    ASSERT(!analyzer->analyze(_T("食べる大人")).empty());
    analyzer.reset();
    cancellation_checks = 0;
    rejected = false;
    try
    {
        store.install(bytes, [&] { return ++cancellation_checks >= 10; });
    }
    catch(const std::exception&)
    {
        rejected = true;
    }
    ASSERT(rejected && store.present());
    store.verify([] { return false; });
    auto corrupt = bytes;
    corrupt[100] ^= 1;
    rejected = false;
    try
    {
        store.install(corrupt, [] { return false; });
    }
    catch(const std::exception&)
    {
        rejected = true;
    }
    ASSERT(rejected && store.present());
    store.verify([] { return false; });
    const auto corrupt_path = store.directory() / L"char.bin";
    std::fstream installed(corrupt_path, std::ios::binary | std::ios::in | std::ios::out);
    char original = 0;
    installed.read(&original, 1);
    const char changed = original ^ 1;
    installed.seekp(0);
    installed.write(&changed, 1);
    installed.close();
    rejected = false;
    try
    {
        store.verify([] { return false; });
    }
    catch(const std::exception&)
    {
        rejected = true;
    }
    ASSERT(rejected && store.present()); // Same size must not be mistaken for integrity.
    store.install(bytes, [] { return false; });
    store.verify([] { return false; });
    std::ofstream(store.directory() / L"user-note.txt") << "must survive";
    rejected = false;
    try
    {
        store.remove();
    }
    catch(const std::exception&)
    {
        rejected = true;
    }
    ASSERT(rejected && store.present());
    std::filesystem::remove(store.directory() / L"user-note.txt");
    store.remove();
    ASSERT(!store.present());
    std::filesystem::remove(std::filesystem::path(root_path) / L"dictionaries");
    std::filesystem::remove(root_path);
}

MVTF_TEST(dictionary_rejects_unsafe_archive_entries_when_requested)
{
    TCHAR archive_path[32768];
    if(!GetEnvironmentVariable(_T("OPENLYRICS_DICTIONARY_ARCHIVE"), archive_path, DWORD(std::size(archive_path))))
        return;
    std::ifstream input(archive_path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    furigana_dictionary::validate_archive(bytes);
    // A central record starts with PK\1\2; scan from the end of the compressed payload.
    size_t central = bytes.size();
    for(size_t pos = 0; pos + 46 < bytes.size(); ++pos)
        if(bytes[pos] == 'P' && bytes[pos + 1] == 'K' && bytes[pos + 2] == 1 && bytes[pos + 3] == 2)
        {
            central = pos;
            break;
        }
    ASSERT(central + 46 < bytes.size());
    const auto rejects = [](const auto& modified)
    {
        try
        {
            furigana_dictionary::validate_archive(modified);
        }
        catch(const std::exception&)
        {
            return true;
        }
        return false;
    };
    auto unsafe = bytes;
    unsafe[central + 46] = '/';
    ASSERT(rejects(unsafe));
    unsafe = bytes;
    unsafe[central + 39] |= 4; // Windows reparse-point attribute (0x400).
    ASSERT(rejects(unsafe));
    unsafe = bytes;
    unsafe[central + 41] = 0xa0; // POSIX symbolic-link mode (0120000 << 16).
    ASSERT(rejects(unsafe));
    unsafe = bytes;
    unsafe[central + 24] ^= 1; // Uncompressed size differs from the trusted file specification.
    ASSERT(rejects(unsafe));
}
#endif
