#include "stdafx.h"

#pragma warning(push, 0)
#include "foobar2000/SDK/coreDarkMode.h"
#include "foobar2000/helpers/atl-misc.h"
#include "resource.h"
#pragma warning(pop)

#include "furigana_dictionary.h"
#include "furigana_service.h"
#include "mvtf/mvtf.h"
#include "preferences.h"
#include "ui_hooks.h"

// clang-format off
const GUID furigana_generation::preferences_guid = {0xe31dca17,0x6d70,0x4938,{0x8a,0x93,0xd1,0x06,0x27,0x9e,0x53,0x72}};
static const GUID generation_enabled_guid = {0xb1fed08a,0x5daa,0x4249,{0x8a,0xe1,0xa2,0x87,0x73,0x0e,0x49,0x9a}};
static const GUID furigana_enabled_guid = {0x8dcae30b,0x51c2,0x45e8,{0xa9,0x11,0x39,0x61,0x24,0x62,0x4e,0xc7}};
// clang-format on
static cfg_bool cfg_generation_enabled(generation_enabled_guid, false);
static cfg_bool cfg_furigana_enabled(furigana_enabled_guid, true);
static void set_furigana_preferences(bool master, bool generated)
{
    if(cfg_furigana_enabled.get_value() == master && cfg_generation_enabled.get_value() == generated) return;
    cfg_furigana_enabled = master;
    cfg_generation_enabled = generated;
    furigana_generation::apply_preferences();
}
bool preferences::furigana::enabled()
{
    return cfg_furigana_enabled.get_value();
}
void preferences::furigana::set_enabled(bool enable)
{
    set_furigana_preferences(enable, cfg_generation_enabled.get_value());
}
bool furigana_generation::enabled()
{
    return cfg_generation_enabled.get_value();
}
void furigana_generation::set_enabled(bool enable)
{
    set_furigana_preferences(preferences::furigana::enabled(), enable);
}

static void update_furigana_controls(CWindow window,
                                     bool master,
                                     bool generated,
                                     const furigana_generation::Status& status)
{
    using furigana_generation::DictionaryState;
    const bool setting_up = status.dictionary == DictionaryState::Downloading
                            || status.dictionary == DictionaryState::Installing;
    window.GetDlgItem(IDC_DICTIONARY_PROGRESS).ShowWindow(setting_up ? SW_SHOWNA : SW_HIDE);
    window.SendDlgItemMessage(
        IDC_DICTIONARY_PROGRESS,
        PBM_SETPOS,
        setting_up ? WPARAM(std::min<uint64_t>(100, status.downloaded * 100 / furigana_dictionary::archive_size)) : 0);
    window.GetDlgItem(IDC_GENERATION_ENABLED)
        .EnableWindow(master
                      && (status.dictionary == DictionaryState::Ready || status.generation_requested || generated));
    window.GetDlgItem(IDC_DICTIONARY_DOWNLOAD).EnableWindow(master && !status.busy());
    window.SetDlgItemText(IDC_DICTIONARY_DOWNLOAD,
                          status.dictionary == DictionaryState::Ready ? _T("Download again and enable")
                                                                      : _T("Download dictionary and enable"));
    window.GetDlgItem(IDC_DICTIONARY_CANCEL)
        .EnableWindow(status.dictionary == DictionaryState::Downloading
                      || status.dictionary == DictionaryState::Installing);
    window.GetDlgItem(IDC_DICTIONARY_REMOVE)
        .EnableWindow(!status.busy() && status.dictionary != DictionaryState::NotInstalled);
}

class PreferencesFurigana : public CDialogImpl<PreferencesFurigana>, public preferences_page_instance
{
public:
    enum
    {
        IDD = IDD_PREFERENCES_FURIGANA
    };
    explicit PreferencesFurigana(preferences_page_callback::ptr callback)
        : m_callback(callback)
    {
    }
    t_uint32 get_state() override
    {
        auto state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if((IsDlgButtonChecked(IDC_GENERATION_ENABLED) == BST_CHECKED) != furigana_generation::enabled()
           || (IsDlgButtonChecked(IDC_FURIGANA_ENABLED) == BST_CHECKED) != preferences::furigana::enabled())
            state |= preferences_state::changed;
        return state;
    }
    void apply() override
    {
        set_furigana_preferences(IsDlgButtonChecked(IDC_FURIGANA_ENABLED) == BST_CHECKED,
                                 IsDlgButtonChecked(IDC_GENERATION_ENABLED) == BST_CHECKED);
        Update();
        m_callback->on_state_changed();
    }
    void reset() override
    {
        CheckDlgButton(IDC_GENERATION_ENABLED, BST_UNCHECKED);
        CheckDlgButton(IDC_FURIGANA_ENABLED, BST_CHECKED);
        Update();
        m_callback->on_state_changed();
    }
    BEGIN_MSG_MAP_EX(PreferencesFurigana)
    MSG_WM_INITDIALOG(OnInitDialog)
    MSG_WM_TIMER(OnTimer)
    COMMAND_HANDLER_EX(IDC_GENERATION_ENABLED, BN_CLICKED, OnChange)
    COMMAND_HANDLER_EX(IDC_FURIGANA_ENABLED, BN_CLICKED, OnChange)
    COMMAND_HANDLER_EX(IDC_DICTIONARY_DOWNLOAD, BN_CLICKED, OnDownload)
    COMMAND_HANDLER_EX(IDC_DICTIONARY_CANCEL, BN_CLICKED, OnCancel)
    COMMAND_HANDLER_EX(IDC_DICTIONARY_REMOVE, BN_CLICKED, OnRemove)
    COMMAND_HANDLER_EX(IDC_DICTIONARY_NOTICES, BN_CLICKED, OnNotices)
    END_MSG_MAP()
private:
    BOOL OnInitDialog(CWindow, LPARAM)
    {
        m_dark.AddDialogWithControls(m_hWnd);
        m_saved = furigana_generation::enabled();
        m_saved_master = preferences::furigana::enabled();
        CheckDlgButton(IDC_GENERATION_ENABLED, m_saved ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_FURIGANA_ENABLED, m_saved_master ? BST_CHECKED : BST_UNCHECKED);
        SetTimer(1, 250);
        Update();
        return FALSE;
    }
    void OnTimer(UINT_PTR)
    {
        Update();
    }
    void OnChange(UINT, int, CWindow)
    {
        Update();
        m_callback->on_state_changed();
    }
    void OnDownload(UINT, int, CWindow)
    {
        if(IsDlgButtonChecked(IDC_FURIGANA_ENABLED) != BST_CHECKED) return;
        apply(); // An explicit setup action commits pending checkbox choices before starting work.
        if(const auto service = furigana_generation::service())
        {
            service->download_and_enable();
            refresh_generated_furigana();
            Update();
        }
    }
    void OnCancel(UINT, int, CWindow)
    {
        if(const auto service = furigana_generation::service())
        {
            service->cancel_setup();
            Update();
        }
    }
    void OnRemove(UINT, int, CWindow)
    {
        furigana_generation::set_enabled(false);
        CheckDlgButton(IDC_GENERATION_ENABLED, BST_UNCHECKED);
        if(const auto service = furigana_generation::service()) service->remove_dictionary();
        refresh_generated_furigana();
        Update();
    }
    void OnNotices(UINT, int, CWindow)
    {
        ShellExecuteW(
            m_hWnd,
            L"open",
            L"https://github.com/taku910/mecab/blob/61b90ba6e669dc2d7d533d4a80d206f3b31d52b1/mecab-ipadic/COPYING",
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
    }
    void Update()
    {
        using namespace furigana_generation;
        const auto service = furigana_generation::service();
        if(!service)
        {
            SetDlgItemText(IDC_DICTIONARY_STATUS, _T("Generation is unavailable."));
            return;
        }
        const auto status = service->status();
        if(m_saved != enabled())
        {
            m_saved = enabled();
            CheckDlgButton(IDC_GENERATION_ENABLED, m_saved ? BST_CHECKED : BST_UNCHECKED);
            m_callback->on_state_changed();
        }
        if(m_saved_master != preferences::furigana::enabled())
        {
            m_saved_master = preferences::furigana::enabled();
            CheckDlgButton(IDC_FURIGANA_ENABLED, m_saved_master ? BST_CHECKED : BST_UNCHECKED);
            m_callback->on_state_changed();
        }
        const TCHAR* text = _T("Not installed");
        switch(status.dictionary)
        {
            case DictionaryState::Ready: text = _T("Ready — IPADIC UTF-8 (2007-08-01), 50.5 MiB installed"); break;
            case DictionaryState::Downloading: text = _T("Downloading Japanese reading dictionary..."); break;
            case DictionaryState::Installing: text = _T("Installing and verifying dictionary..."); break;
            case DictionaryState::Checking: text = _T("Checking dictionary..."); break;
            case DictionaryState::Removing: text = _T("Removing dictionary..."); break;
            case DictionaryState::NeedsRepair: text = _T("Needs repair"); break;
            default: break;
        }
        std::tstring message = text;
        if(!status.error.empty()) message += _T("\r\n") + to_tstring(status.error);
        SetDlgItemText(IDC_DICTIONARY_STATUS, message.c_str());
        update_furigana_controls(m_hWnd,
                                 IsDlgButtonChecked(IDC_FURIGANA_ENABLED) == BST_CHECKED,
                                 IsDlgButtonChecked(IDC_GENERATION_ENABLED) == BST_CHECKED,
                                 status);
    }
    preferences_page_callback::ptr m_callback;
    fb2k::CCoreDarkModeHooks m_dark;
    bool m_saved = false;
    bool m_saved_master = true;
};

class PreferencesFuriganaImpl : public preferences_page_impl<PreferencesFurigana>
{
public:
    const char* get_name() override
    {
        return "Furigana";
    }
    GUID get_guid() override
    {
        return furigana_generation::preferences_guid;
    }
    GUID get_parent_guid() override
    {
        return GUID_PREFERENCES_PAGE_ROOT;
    }
};
static preferences_page_factory_t<PreferencesFuriganaImpl> g_furigana_preferences;

#if MVTF_TESTS_ENABLED
#include "parsers.h"
#include "ruby_test.h"

MVTF_TEST(furigana_master_preferences_preserve_generation_choice_and_metadata)
{
    struct Restore
    {
        bool master = preferences::furigana::enabled();
        bool generated = furigana_generation::enabled();
        ~Restore()
        {
            set_furigana_preferences(master, generated);
        }
    } restore;
    for(const bool master : { false, true })
        for(const bool generated : { false, true })
        {
            set_furigana_preferences(master, generated);
            ASSERT(preferences::display::show_furigana() == master
                   && furigana_generation::active() == (master && generated));
            preferences::furigana::set_enabled(!master);
            ASSERT(furigana_generation::enabled() == generated);
            preferences::furigana::set_enabled(master);
            const auto lyrics = parsers::lrc::parse({}, "[00:01.00]日\n\t[kana:1ひ]\t");
            ASSERT(lyrics.has_kana_metadata && lyrics.kana_metadata_valid && lyrics.lines.size() == 1
                   && lyrics.lines[0].furigana.size() == 1);
            const auto saved = parsers::lrc::expand_text(lyrics, false);
            ASSERT(saved.find(_T("[kana:1ひ]")) != saved.npos);
        }
}

MVTF_TEST(furigana_preferences_resource_and_offscreen_states)
{
    INITCOMMONCONTROLSEX controls { sizeof(controls), ICC_PROGRESS_CLASS };
    ASSERT(InitCommonControlsEx(&controls));
    const auto instance = GetModuleHandle(_T("foo_openlyrics.dll"));
    HWND parent =
        CreateWindowEx(0, _T("STATIC"), _T(""), WS_POPUP, 0, 0, 800, 600, nullptr, nullptr, instance, nullptr);
    ASSERT(parent != nullptr);
    HWND dialog = CreateDialogParam(
        instance,
        MAKEINTRESOURCE(IDD_PREFERENCES_FURIGANA),
        parent,
        [](HWND, UINT, WPARAM, LPARAM) -> INT_PTR { return FALSE; },
        0);
    ASSERT(dialog != nullptr);
    RECT area {};
    GetClientRect(dialog, &area);
    bool fits = true;
    for(HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
    {
        RECT bounds {};
        GetWindowRect(child, &bounds);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&bounds), 2);
        fits &= bounds.left >= 0 && bounds.top >= 0 && bounds.right <= area.right && bounds.bottom <= area.bottom;
    }
    ASSERT(fits);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = area.right;
    info.bmiHeader.biHeight = -area.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    ASSERT(bitmap != nullptr);
    HGDIOBJ old = SelectObject(dc, bitmap);
    const TCHAR* states[] = { _T("Not installed"),
                              _T("Ready — IPADIC UTF-8 (2007-08-01), 50.5 MiB installed"),
                              _T("Downloading Japanese reading dictionary..."),
                              _T("Needs repair\r\nDictionary download failed. Check the network and retry.") };
    for(int i = 0; i < 4; ++i)
    {
        SetDlgItemText(dialog, IDC_DICTIONARY_STATUS, states[i]);
        furigana_generation::Status status;
        status.dictionary = i == 0   ? furigana_generation::DictionaryState::NotInstalled
                            : i == 1 ? furigana_generation::DictionaryState::Ready
                            : i == 2 ? furigana_generation::DictionaryState::Downloading
                                     : furigana_generation::DictionaryState::NeedsRepair;
        status.downloaded = i == 2 ? furigana_dictionary::archive_size * 55 / 100 : furigana_dictionary::archive_size;
        CheckDlgButton(dialog, IDC_FURIGANA_ENABLED, BST_CHECKED);
        update_furigana_controls(dialog, true, false, status);
        ASSERT(((GetWindowLongPtr(GetDlgItem(dialog, IDC_DICTIONARY_PROGRESS), GWL_STYLE) & WS_VISIBLE) != 0)
               == (i == 2));
        if(i != 2) ASSERT(SendDlgItemMessage(dialog, IDC_DICTIONARY_PROGRESS, PBM_GETPOS, 0, 0) == 0);
        SendMessage(dialog, WM_PRINT, WPARAM(dc), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
        GdiFlush();
        const auto name = std::format(_T("furigana-preferences-{}.bmp"), i);
        ASSERT(save_ruby_test_bitmap(name.c_str(),
                                     area.right,
                                     area.bottom,
                                     area.right * 4,
                                     static_cast<uint8_t*>(pixels)));
    }
    furigana_generation::Status disabled;
    disabled.dictionary = furigana_generation::DictionaryState::Ready;
    disabled.generation_requested = true;
    SetDlgItemText(dialog, IDC_DICTIONARY_STATUS, states[1]);
    CheckDlgButton(dialog, IDC_FURIGANA_ENABLED, BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_GENERATION_ENABLED, BST_CHECKED);
    update_furigana_controls(dialog, false, true, disabled);
    ASSERT(!IsWindowEnabled(GetDlgItem(dialog, IDC_GENERATION_ENABLED))
           && !IsWindowEnabled(GetDlgItem(dialog, IDC_DICTIONARY_DOWNLOAD))
           && IsWindowEnabled(GetDlgItem(dialog, IDC_DICTIONARY_REMOVE))
           && IsDlgButtonChecked(dialog, IDC_GENERATION_ENABLED) == BST_CHECKED);
    SendMessage(dialog, WM_PRINT, WPARAM(dc), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    GdiFlush();
    ASSERT(save_ruby_test_bitmap(_T("furigana-preferences-master-off.bmp"),
                                 area.right,
                                 area.bottom,
                                 area.right * 4,
                                 static_cast<uint8_t*>(pixels)));
    disabled.dictionary = furigana_generation::DictionaryState::Removing;
    disabled.downloaded = furigana_dictionary::archive_size;
    update_furigana_controls(dialog, false, true, disabled);
    ASSERT((GetWindowLongPtr(GetDlgItem(dialog, IDC_DICTIONARY_PROGRESS), GWL_STYLE) & WS_VISIBLE) == 0);
    ASSERT(SendDlgItemMessage(dialog, IDC_DICTIONARY_PROGRESS, PBM_GETPOS, 0, 0) == 0);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DestroyWindow(dialog);
    DestroyWindow(parent);
}
#endif
