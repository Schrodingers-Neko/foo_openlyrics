#include "stdafx.h"

#include "furigana_dictionary.h"
#include "furigana_service.h"
#include "logging.h"
#include "mvtf/mvtf.h"
#include "ui_hooks.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

class furigana_generation::Service::Impl
{
public:
    Impl(std::filesystem::path root, Post post, Changed changed)
        : store(std::move(root))
        , post(std::move(post))
        , changed(std::move(changed))
    {
        state.dictionary = store.present() ? DictionaryState::Ready : DictionaryState::NotInstalled;
    }
    furigana_dictionary::Store store;
    Post post;
    Changed changed;
    mutable std::mutex mutex;
    std::condition_variable wake;
    Status state;
    std::atomic<uint64_t> epoch { 1 };
    std::atomic<bool> stopping { false };
    std::thread worker;
    std::deque<std::function<void()>> tasks;
    std::unique_ptr<ReadingAnalyzer> analyzer;
    AnnotationCache cache;
    struct Subscriber
    {
        Complete complete;
        std::function<bool()> obsolete;
    };
    struct Pending
    {
        LyricData source;
        bool force;
        uint64_t epoch;
        std::vector<Subscriber> callbacks;
    };
    std::vector<std::shared_ptr<Pending>> pending;

    void enqueue(std::function<void()> task)
    {
        std::lock_guard lock(mutex);
        if(stopping) return;
        if(!worker.joinable())
            worker = std::thread(
                [this]
                {
                    for(;;)
                    {
                        std::function<void()> next;
                        {
                            std::unique_lock guard(mutex);
                            wake.wait(guard, [&] { return stopping || !tasks.empty(); });
                            if(stopping) break;
                            next = std::move(tasks.front());
                            tasks.pop_front();
                        }
                        next();
                    }
                    analyzer.reset();
                    cache.clear();
                });
        // Obsolete per-panel requests can outlive their subscribers briefly; bound that queue too.
        if(tasks.size() >= 32) tasks.pop_front();
        tasks.push_back(std::move(task));
        wake.notify_one();
    }

    void invalidate()
    {
        ++epoch;
        std::lock_guard lock(mutex);
        tasks.clear();
        pending.clear();
    }

    void notify(std::weak_ptr<Service> weak, uint64_t revision, bool setup_succeeded = false)
    {
        if(stopping || epoch != revision) return;
        post(
            [weak, revision, setup_succeeded]
            {
                if(const auto owner = weak.lock())
                {
                    auto& self = *owner->m_impl;
                    if(self.stopping || self.epoch != revision) return;
                    self.changed(setup_succeeded);
                }
            });
    }
};

furigana_generation::Service::Service(std::filesystem::path root, Post post, Changed changed)
    : m_impl(std::make_unique<Impl>(std::move(root), std::move(post), std::move(changed)))
{
}
furigana_generation::Service::~Service()
{
    shutdown();
}
furigana_generation::Status furigana_generation::Service::status() const
{
    std::lock_guard lock(m_impl->mutex);
    return m_impl->state;
}

void furigana_generation::Service::set_enabled(bool enable)
{
    set_policy(status().master_enabled, enable);
}

void furigana_generation::Service::set_policy(bool master_enabled, bool generation_requested)
{
    apply_policy(master_enabled, generation_requested, false);
}

void furigana_generation::Service::apply_policy(bool master_enabled, bool generation_requested, bool force)
{
    auto& self = *m_impl;
    const auto previous = status();
    if(!force && previous.master_enabled == master_enabled && previous.generation_requested == generation_requested)
        return;
    const bool enable = master_enabled && generation_requested;
    self.invalidate();
    {
        std::lock_guard lock(self.mutex);
        self.state.enabled = enable;
        self.state.master_enabled = master_enabled;
        self.state.generation_requested = generation_requested;
        self.state.downloaded = 0;
        self.state.dictionary = self.store.present() ? DictionaryState::Ready
                                : enable             ? DictionaryState::NeedsRepair
                                                     : DictionaryState::NotInstalled;
        self.state.error.clear();
    }
    const auto weak = weak_from_this();
    const auto revision = self.epoch.load();
    if(!enable && !self.worker.joinable())
    {
        self.notify(weak, revision);
        return;
    }
    self.enqueue(
        [impl = &self, weak, revision]
        {
            impl->analyzer.reset();
            impl->cache.clear();
            impl->notify(weak, revision);
        });
}

void furigana_generation::Service::download_and_enable()
{
    auto& self = *m_impl;
    if(!status().master_enabled || status().busy()) return;
    const bool was_enabled = status().enabled;
    self.invalidate();
    const auto revision = self.epoch.load();
    const auto weak = weak_from_this();
    {
        std::lock_guard lock(self.mutex);
        self.state.dictionary = DictionaryState::Downloading;
        self.state.error.clear();
        self.state.downloaded = 0;
    }
    self.enqueue(
        [implementation = &self, weak, revision, was_enabled]
        {
            auto& impl = *implementation;
            const auto cancelled = [&] { return impl.stopping || impl.epoch != revision; };
            bool setup_succeeded = false;
            try
            {
                impl.analyzer.reset();
                impl.cache.clear();
                const auto data = furigana_dictionary::download(cancelled,
                                                                [&](uint64_t count, uint64_t)
                                                                {
                                                                    std::lock_guard lock(impl.mutex);
                                                                    if(impl.epoch == revision)
                                                                        impl.state.downloaded = count;
                                                                });
                {
                    std::lock_guard lock(impl.mutex);
                    if(cancelled()) return;
                    impl.state.dictionary = DictionaryState::Installing;
                }
                impl.store.install(data, cancelled);
                {
                    std::lock_guard lock(impl.mutex);
                    if(cancelled()) return;
                    impl.state.dictionary = DictionaryState::Ready;
                    impl.state.downloaded = 0;
                    impl.state.enabled = true;
                    impl.state.generation_requested = true;
                    setup_succeeded = true;
                }
            }
            catch(const std::exception& error)
            {
                std::lock_guard lock(impl.mutex);
                if(cancelled()) return;
                const bool present = impl.store.present();
                impl.state.dictionary = present ? DictionaryState::Ready : DictionaryState::NeedsRepair;
                impl.state.enabled = was_enabled && present;
                impl.state.error = error.what();
                impl.state.downloaded = 0;
            }
            impl.notify(weak, revision, setup_succeeded);
        });
}

void furigana_generation::Service::cancel_setup()
{
    const auto current = status();
    apply_policy(current.master_enabled, current.generation_requested, true);
}

void furigana_generation::Service::remove_dictionary()
{
    auto& self = *m_impl;
    self.invalidate();
    const auto revision = self.epoch.load();
    const auto weak = weak_from_this();
    {
        std::lock_guard lock(self.mutex);
        self.state.enabled = false;
        self.state.generation_requested = false;
        self.state.downloaded = 0;
        self.state.dictionary = DictionaryState::Removing;
        self.state.error.clear();
    }
    self.enqueue(
        [implementation = &self, weak, revision]
        {
            auto& impl = *implementation;
            impl.analyzer.reset();
            impl.cache.clear();
            try
            {
                if(impl.stopping || impl.epoch != revision) return;
                impl.store.remove();
                std::lock_guard lock(impl.mutex);
                if(impl.epoch == revision) impl.state.dictionary = DictionaryState::NotInstalled;
            }
            catch(const std::exception& error)
            {
                std::lock_guard lock(impl.mutex);
                if(impl.epoch == revision)
                {
                    impl.state.dictionary = DictionaryState::NeedsRepair;
                    impl.state.error = error.what();
                }
            }
            impl.notify(weak, revision);
        });
}

void furigana_generation::Service::request(const LyricData& source,
                                           bool force,
                                           Complete complete,
                                           std::function<bool()> obsolete)
{
    const auto current = status();
    if(!current.enabled || current.dictionary != DictionaryState::Ready || !eligible(source, force) || obsolete())
        return;
    size_t length = 0;
    for(const auto& line : source.lines)
    {
        if(line.text.size() > 65536 - length) return;
        length += line.text.size();
    }
    auto& self = *m_impl;
    const auto revision = self.epoch.load();
    const auto weak = weak_from_this();
    std::shared_ptr<Impl::Pending> request;
    {
        std::lock_guard lock(self.mutex);
        std::erase_if(self.pending,
                      [](const auto& old)
                      {
                          return std::all_of(old->callbacks.begin(),
                                             old->callbacks.end(),
                                             [](const auto& subscriber) { return subscriber.obsolete(); });
                      });
        for(const auto& pending : self.pending)
        {
            if(pending->force != force || pending->epoch != revision
               || pending->source.lines.size() != source.lines.size())
                continue;
            bool same = true;
            for(size_t i = 0; i < source.lines.size(); ++i)
                same &= pending->source.lines[i].text == source.lines[i].text;
            if(same)
            {
                pending->callbacks.push_back({ std::move(complete), std::move(obsolete) });
                return;
            }
        }
        // Current panels only; do not retain an unbounded queue of obsolete tracks.
        if(self.pending.size() >= 16) return;
        request = std::make_shared<Impl::Pending>(
            Impl::Pending { source, force, revision, { { std::move(complete), std::move(obsolete) } } });
        self.pending.push_back(request);
    }
    self.enqueue(
        [implementation = &self, weak, request]
        {
            auto& impl = *implementation;
            const auto cancelled = [&]
            {
                if(impl.stopping || impl.epoch != request->epoch) return true;
                std::lock_guard lock(impl.mutex);
                return std::all_of(request->callbacks.begin(),
                                   request->callbacks.end(),
                                   [](const auto& subscriber) { return subscriber.obsolete(); });
            };
            GeneratedAnnotations result;
            try
            {
                if(cancelled())
                {
                    std::lock_guard lock(impl.mutex);
                    std::erase(impl.pending, request);
                    return;
                }
                if(!impl.analyzer)
                {
                    impl.store.verify(cancelled);
                    impl.analyzer = create_analyzer(impl.store.directory().native());
                }
                result = generate(*impl.analyzer, request->source, request->force, cancelled, &impl.cache);
            }
            catch(const std::exception& error)
            {
                if(cancelled())
                {
                    std::lock_guard lock(impl.mutex);
                    std::erase(impl.pending, request);
                    return;
                }
                {
                    std::lock_guard lock(impl.mutex);
                    impl.state.dictionary = DictionaryState::NeedsRepair;
                    impl.state.error = error.what();
                    std::erase(impl.pending, request);
                }
                impl.analyzer.reset();
                impl.cache.clear();
                impl.notify(weak, request->epoch);
                return;
            }
            if(cancelled())
            {
                std::lock_guard lock(impl.mutex);
                std::erase(impl.pending, request);
                return;
            }
            impl.post(
                [weak, request, result = std::move(result)]() mutable
                {
                    const auto active = weak.lock();
                    if(!active) return;
                    auto& state = *active->m_impl;
                    std::vector<Impl::Subscriber> callbacks;
                    {
                        std::lock_guard lock(state.mutex);
                        std::erase(state.pending, request);
                        if(state.stopping || state.epoch != request->epoch || !state.state.enabled) return;
                        callbacks = std::move(request->callbacks);
                    }
                    for(const auto& subscriber : callbacks)
                        if(!subscriber.obsolete()) subscriber.complete(result);
                });
        });
}

void furigana_generation::Service::shutdown()
{
    auto& self = *m_impl;
    if(self.stopping.exchange(true)) return;
    ++self.epoch;
    {
        std::lock_guard lock(self.mutex);
        self.tasks.clear();
        self.pending.clear();
    }
    self.wake.notify_all();
    if(self.worker.joinable()) self.worker.join();
}

namespace
{
    std::shared_ptr<furigana_generation::Service> g_generation_service;
    void initialise_generation()
    {
        pfc::string8 native;
        if(!filesystem::g_get_native_path(core_api::get_profile_path(), native)) return;
        const std::filesystem::path root = std::filesystem::path(
                                               to_tstring(std::string_view(native.get_ptr(), native.get_length())))
                                           / L"openlyrics" / L"furigana";
        g_generation_service = std::make_shared<furigana_generation::Service>(
            root,
            [](auto callback) { fb2k::inMainThread2(std::move(callback)); },
            [](bool setup_succeeded)
            {
                if(setup_succeeded && preferences::furigana::enabled() && !furigana_generation::enabled())
                {
                    furigana_generation::set_enabled(true);
                    return;
                }
                refresh_generated_furigana();
            });
        g_generation_service->set_policy(preferences::furigana::enabled(), furigana_generation::enabled());
    }
}
FB2K_RUN_ON_INIT(initialise_generation);
FB2K_RUN_ON_QUIT(furigana_generation::shutdown);

std::shared_ptr<furigana_generation::Service> furigana_generation::service()
{
    return g_generation_service;
}
bool furigana_generation::active()
{
    return preferences::furigana::enabled() && enabled();
}
void furigana_generation::apply_preferences()
{
    if(g_generation_service) g_generation_service->set_policy(preferences::furigana::enabled(), enabled());
    refresh_generated_furigana();
}
void furigana_generation::shutdown()
{
    if(g_generation_service) g_generation_service->shutdown();
    g_generation_service.reset();
}
void furigana_generation::show_preferences()
{
    ui_control::get()->show_preferences(preferences_guid);
}

#if MVTF_TESTS_ENABLED
MVTF_TEST(generation_service_disabled_does_not_create_profile)
{
    const auto root = std::filesystem::temp_directory_path()
                      / std::format(L"openlyrics-unused-{}-{}", GetCurrentProcessId(), GetTickCount64());
    std::vector<std::function<void()>> posted;
    auto instance = std::make_shared<furigana_generation::Service>(
        root,
        [&](auto callback) { posted.push_back(std::move(callback)); },
        [](bool) {});
    LyricData lyrics {};
    lyrics.lines = { { _T("今日の歌"), 1.0 } };
    instance->request(lyrics, false, [](auto) {});
    ASSERT(!instance->status().enabled
           && instance->status().dictionary == furigana_generation::DictionaryState::NotInstalled);
    ASSERT(posted.empty() && !std::filesystem::exists(root));
    instance->shutdown();
    ASSERT(!std::filesystem::exists(root));
}

MVTF_TEST(generation_master_off_blocks_setup_and_keeps_generation_choice)
{
    using namespace furigana_generation;
    const auto root = std::filesystem::temp_directory_path()
                      / std::format(L"openlyrics-master-off-{}-{}", GetCurrentProcessId(), GetTickCount64());
    std::vector<std::function<void()>> posted;
    bool setup_succeeded = false;
    auto instance = std::make_shared<Service>(
        root,
        [&](auto callback) { posted.push_back(std::move(callback)); },
        [&](bool setup) { setup_succeeded |= setup; });
    instance->set_policy(false, true);
    instance->download_and_enable(); // A master-off service must not start network or file work.
    LyricData lyrics {};
    lyrics.lines = { { _T("今日の歌"), 1.0 } };
    bool received = false;
    instance->request(lyrics, true, [&](auto) { received = true; });
    const auto status = instance->status();
    ASSERT(!status.enabled && !status.master_enabled && status.generation_requested && !status.busy()
           && status.downloaded == 0 && !std::filesystem::exists(root));
    for(auto& callback : posted)
        callback();
    ASSERT(!received && !setup_succeeded);
    instance->shutdown();
}

MVTF_TEST(generation_service_coalesces_and_rejects_stale_results_when_requested)
{
    using namespace furigana_generation;
    TCHAR archive_path[32768], root_path[32768];
    if(!GetEnvironmentVariable(_T("OPENLYRICS_DICTIONARY_ARCHIVE"), archive_path, DWORD(std::size(archive_path)))
       || !GetEnvironmentVariable(_T("OPENLYRICS_DICTIONARY_TEST_ROOT"), root_path, DWORD(std::size(root_path))))
        return;
    const auto root = std::filesystem::path(std::tstring(root_path) + _T("-service"));
    std::ifstream input(archive_path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    furigana_dictionary::Store store(root);
    store.install(bytes, [] { return false; });
    std::mutex queue_mutex;
    std::condition_variable changed;
    std::deque<std::function<void()>> queue;
    int notifications = 0;
    bool setup_succeeded = false;
    auto instance = std::make_shared<Service>(
        root,
        [&](auto callback)
        {
            std::lock_guard lock(queue_mutex);
            queue.push_back(std::move(callback));
            changed.notify_one();
        },
        [&](bool setup)
        {
            ++notifications;
            setup_succeeded |= setup;
        });
    const auto drain = [&](const std::function<bool()>& done)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while(!done() && std::chrono::steady_clock::now() < deadline)
        {
            std::function<void()> callback;
            {
                std::unique_lock lock(queue_mutex);
                changed.wait_until(lock, deadline, [&] { return !queue.empty(); });
                if(queue.empty()) break;
                callback = std::move(queue.front());
                queue.pop_front();
            }
            callback();
        }
        return done();
    };
    instance->set_enabled(true);
    LyricData lyrics {};
    lyrics.lines = { { _T("今日の日々を取り戻す"), 1.0 }, { _T("今日の日々を取り戻す"), 3.0 } };
    int received = 0;
    GeneratedAnnotations result;
    const auto receive = [&](auto data)
    {
        ++received;
        result = std::move(data);
    };
    instance->request(lyrics, false, receive);
    instance->request(lyrics, false, receive);
    ASSERT(drain([&] { return received == 2; }));
    ASSERT(result.size() == 2 && result[0] == result[1] && result[0].size() == 4);
    ASSERT(lyrics.lines[0].furigana.empty() && !lyrics.has_kana_metadata);
    instance->request(lyrics, true, receive);
    const int previous_notifications = notifications;
    instance->set_policy(false, true);
    ASSERT(drain([&] { return notifications > previous_notifications; }));
    ASSERT(received == 2 && !instance->status().enabled && instance->status().generation_requested && store.present()
           && !setup_succeeded);
    instance->set_policy(true, true);
    instance->request(lyrics, false, receive);
    ASSERT(drain([&] { return received == 3; }));
    ASSERT(result[0].size() == 4 && instance->status().enabled && store.present());
    std::atomic<bool> obsolete { false };
    instance->request(lyrics, true, receive, [&] { return obsolete.load(); });
    obsolete = true;
    instance->set_enabled(false);
    instance->shutdown();
    while(!queue.empty())
    {
        auto callback = std::move(queue.front());
        queue.pop_front();
        callback();
    }
    ASSERT(received == 3 && !instance->status().enabled);
    store.verify([] { return false; });
    store.remove();
    std::filesystem::remove(root / L"dictionaries");
    std::filesystem::remove(root);
}
#endif
