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
// clang-format on
static cfg_bool cfg_generation_enabled(generation_enabled_guid, false);
bool furigana_generation::enabled()
{
    return cfg_generation_enabled.get_value();
}
void furigana_generation::set_enabled(bool enable)
{
    cfg_generation_enabled = enable;
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
        if((IsDlgButtonChecked(IDC_GENERATION_ENABLED) == BST_CHECKED) != furigana_generation::enabled())
            state |= preferences_state::changed;
        return state;
    }
    void apply() override
    {
        const bool value = IsDlgButtonChecked(IDC_GENERATION_ENABLED) == BST_CHECKED;
        furigana_generation::set_enabled(value);
        if(const auto service = furigana_generation::service()) service->set_enabled(value);
        refresh_generated_furigana();
        m_callback->on_state_changed();
    }
    void reset() override
    {
        CheckDlgButton(IDC_GENERATION_ENABLED, BST_UNCHECKED);
        m_callback->on_state_changed();
    }
    BEGIN_MSG_MAP_EX(PreferencesFurigana)
    MSG_WM_INITDIALOG(OnInitDialog)
    MSG_WM_TIMER(OnTimer)
    COMMAND_HANDLER_EX(IDC_GENERATION_ENABLED, BN_CLICKED, OnChange)
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
        CheckDlgButton(IDC_GENERATION_ENABLED, m_saved ? BST_CHECKED : BST_UNCHECKED);
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
        m_callback->on_state_changed();
    }
    void OnDownload(UINT, int, CWindow)
    {
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
        GetDlgItem(IDC_GENERATION_ENABLED).EnableWindow(status.dictionary == DictionaryState::Ready || enabled());
        GetDlgItem(IDC_DICTIONARY_DOWNLOAD).EnableWindow(!status.busy());
        SetDlgItemText(IDC_DICTIONARY_DOWNLOAD,
                       status.dictionary == DictionaryState::Ready ? _T("Download again and enable")
                                                                   : _T("Download dictionary and enable"));
        GetDlgItem(IDC_DICTIONARY_CANCEL)
            .EnableWindow(status.dictionary == DictionaryState::Downloading
                          || status.dictionary == DictionaryState::Installing);
        GetDlgItem(IDC_DICTIONARY_REMOVE)
            .EnableWindow(!status.busy() && status.dictionary != DictionaryState::NotInstalled);
        SendDlgItemMessage(
            IDC_DICTIONARY_PROGRESS,
            PBM_SETPOS,
            WPARAM(std::min<uint64_t>(100, status.downloaded * 100 / furigana_dictionary::archive_size)));
    }
    preferences_page_callback::ptr m_callback;
    fb2k::CCoreDarkModeHooks m_dark;
    bool m_saved = false;
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
#include "ruby_test.h"

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
        EnableWindow(GetDlgItem(dialog, IDC_GENERATION_ENABLED), i == 1);
        EnableWindow(GetDlgItem(dialog, IDC_DICTIONARY_DOWNLOAD), i != 2);
        EnableWindow(GetDlgItem(dialog, IDC_DICTIONARY_CANCEL), i == 2);
        EnableWindow(GetDlgItem(dialog, IDC_DICTIONARY_REMOVE), i == 1 || i == 3);
        SendDlgItemMessage(dialog, IDC_DICTIONARY_PROGRESS, PBM_SETPOS, i == 2 ? 55 : 0, 0);
        SendMessage(dialog, WM_PRINT, WPARAM(dc), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
        GdiFlush();
        const auto name = std::format(_T("furigana-preferences-{}.bmp"), i);
        ASSERT(save_ruby_test_bitmap(name.c_str(),
                                     area.right,
                                     area.bottom,
                                     area.right * 4,
                                     static_cast<uint8_t*>(pixels)));
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DestroyWindow(dialog);
    DestroyWindow(parent);
}
#endif
