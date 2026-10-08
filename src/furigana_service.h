#pragma once

#include "furigana_generator.h"
#include <filesystem>

namespace furigana_generation
{
    enum class DictionaryState
    {
        NotInstalled,
        Ready,
        Downloading,
        Installing,
        Checking,
        Removing,
        NeedsRepair
    };
    struct Status
    {
        DictionaryState dictionary = DictionaryState::NotInstalled;
        bool enabled = false;
        uint64_t downloaded = 0;
        std::string error;
        bool busy() const
        {
            return dictionary == DictionaryState::Downloading || dictionary == DictionaryState::Installing
                   || dictionary == DictionaryState::Checking || dictionary == DictionaryState::Removing;
        }
    };
    using GeneratedAnnotations = std::vector<std::vector<FuriganaSpan>>;
    class Service : public std::enable_shared_from_this<Service>
    {
    public:
        using Post = std::function<void(std::function<void()>)>;
        using Changed = std::function<void(bool)>;
        using Complete = std::function<void(GeneratedAnnotations)>;
        Service(std::filesystem::path root, Post post, Changed changed);
        ~Service();
        Status status() const;
        void set_enabled(bool enabled);
        void download_and_enable();
        void cancel_setup();
        void remove_dictionary();
        void request(
            const LyricData& source,
            bool force_japanese,
            Complete complete,
            std::function<bool()> obsolete = [] { return false; });
        void shutdown();

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };

    // Host facade: main-thread callers only. Setup starts solely from explicit UI actions.
    std::shared_ptr<Service> service();
    void shutdown();
    void set_enabled(bool enabled);
    bool enabled();
    void show_preferences();
    extern const GUID preferences_guid;
}
