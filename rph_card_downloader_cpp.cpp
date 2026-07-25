#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <urlmon.h>

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "urlmon.lib")

namespace {

constexpr int ID_SEARCH = 1001;
constexpr int ID_LIST = 1002;
constexpr int ID_DOWNLOAD = 1003;
constexpr int ID_OPEN_FOLDER = 1004;
constexpr int ID_CHOOSE_FOLDER = 1005;
constexpr UINT WM_STATUS = WM_APP + 1;
constexpr UINT WM_DOWNLOAD_DONE = WM_APP + 2;
constexpr int CARD_CATALOG = 101;
constexpr COLORREF COLOR_APP_BACKGROUND = RGB(255, 248, 252);
constexpr COLORREF COLOR_INK = RGB(71, 54, 70);
constexpr COLORREF COLOR_MUTED = RGB(126, 103, 121);
constexpr COLORREF COLOR_PINK = RGB(244, 109, 157);
constexpr COLORREF COLOR_MINT = RGB(91, 183, 163);
constexpr COLORREF COLOR_GOLD = RGB(235, 172, 70);

struct Card {
    std::wstring id;
    std::wstring name;
};

HWND g_window = nullptr;
HWND g_search = nullptr;
HWND g_list = nullptr;
HWND g_status = nullptr;
HWND g_download = nullptr;
HWND g_open_folder = nullptr;
HWND g_choose_folder = nullptr;
HWND g_title = nullptr;
HWND g_subtitle = nullptr;
HWND g_count = nullptr;
HWND g_folder_label = nullptr;
HFONT g_title_font = nullptr;
HFONT g_body_font = nullptr;
HFONT g_small_font = nullptr;
HFONT g_button_font = nullptr;
HBRUSH g_background = nullptr;
std::vector<Card> g_cards;
std::vector<size_t> g_visible_cards;
std::filesystem::path g_download_folder;

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return L"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string AppendUtf8Codepoint(unsigned int codepoint) {
    std::string result;
    if (codepoint <= 0x7F) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return result;
}

bool ReadJsonString(const std::string& source, size_t& position, std::string& value) {
    if (position >= source.size() || source[position] != '"') return false;
    ++position;
    value.clear();
    while (position < source.size()) {
        const char current = source[position++];
        if (current == '"') return true;
        if (current != '\\') {
            value.push_back(current);
            continue;
        }
        if (position >= source.size()) return false;
        const char escaped = source[position++];
        switch (escaped) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u': {
                if (position + 4 > source.size()) return false;
                unsigned int codepoint = 0;
                for (int index = 0; index < 4; ++index) {
                    const char hex = source[position++];
                    codepoint <<= 4;
                    if (hex >= '0' && hex <= '9') codepoint |= hex - '0';
                    else if (hex >= 'a' && hex <= 'f') codepoint |= hex - 'a' + 10;
                    else if (hex >= 'A' && hex <= 'F') codepoint |= hex - 'A' + 10;
                    else return false;
                }
                value += AppendUtf8Codepoint(codepoint);
                break;
            }
            default: return false;
        }
    }
    return false;
}

std::vector<Card> ParseCards(const std::string& source) {
    std::vector<Card> cards;
    const std::string id_key = "\"id\":\"";
    const std::string name_key = "\"name\":\"";
    size_t cursor = 0;
    while (true) {
        const size_t id_start = source.find(id_key, cursor);
        if (id_start == std::string::npos) break;
        size_t id_position = id_start + id_key.size() - 1;
        std::string id;
        if (!ReadJsonString(source, id_position, id)) break;

        const size_t next_id = source.find(id_key, id_position);
        const size_t name_start = source.find(name_key, id_position);
        if (name_start == std::string::npos || (next_id != std::string::npos && name_start > next_id)) {
            cursor = id_position;
            continue;
        }
        size_t name_position = name_start + name_key.size() - 1;
        std::string name;
        if (ReadJsonString(source, name_position, name)) {
            cards.push_back({Utf8ToWide(id), Utf8ToWide(name)});
        }
        cursor = name_position;
    }
    return cards;
}

bool LoadCatalogResource() {
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(CARD_CATALOG), RT_RCDATA);
    if (!resource) return false;
    const HGLOBAL loaded = LoadResource(nullptr, resource);
    const DWORD bytes = SizeofResource(nullptr, resource);
    const auto* data = static_cast<const char*>(LockResource(loaded));
    if (!data || bytes == 0) return false;
    g_cards = ParseCards(std::string(data, data + bytes));
    return !g_cards.empty();
}

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

void SetStatus(const std::wstring& text) {
    SetWindowTextW(g_status, text.c_str());
}

COLORREF ButtonColor(UINT control_id) {
    if (control_id == ID_DOWNLOAD) return COLOR_PINK;
    if (control_id == ID_CHOOSE_FOLDER) return COLOR_MINT;
    return COLOR_GOLD;
}

COLORREF Darken(COLORREF color, int amount) {
    return RGB(std::max(0, static_cast<int>(GetRValue(color)) - amount),
        std::max(0, static_cast<int>(GetGValue(color)) - amount),
        std::max(0, static_cast<int>(GetBValue(color)) - amount));
}

void DrawAnimeButton(const DRAWITEMSTRUCT* draw) {
    RECT area = draw->rcItem;
    const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
    const COLORREF base = ButtonColor(draw->CtlID);
    const COLORREF fill = pressed ? Darken(base, 18) : base;
    const int offset = pressed ? 1 : 0;
    OffsetRect(&area, 0, offset);

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, Darken(base, 28));
    const auto old_brush = SelectObject(draw->hDC, brush);
    const auto old_pen = SelectObject(draw->hDC, pen);
    RoundRect(draw->hDC, area.left, area.top, area.right, area.bottom, 14, 14);
    SelectObject(draw->hDC, old_brush);
    SelectObject(draw->hDC, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);

    wchar_t text[128]{};
    GetWindowTextW(draw->hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, RGB(255, 255, 255));
    const auto old_font = SelectObject(draw->hDC, g_button_font);
    DrawTextW(draw->hDC, text, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(draw->hDC, old_font);
}

void UpdateFolderLabel() {
    SetWindowTextW(g_folder_label, (L"Saving to: " + g_download_folder.wstring()).c_str());
}

void FillList() {
    wchar_t buffer[512]{};
    GetWindowTextW(g_search, buffer, static_cast<int>(std::size(buffer)));
    const std::wstring query = ToLower(buffer);
    g_visible_cards.clear();
    ListView_DeleteAllItems(g_list);

    for (size_t index = 0; index < g_cards.size(); ++index) {
        if (!query.empty() && ToLower(g_cards[index].name).find(query) == std::wstring::npos) continue;
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = ListView_GetItemCount(g_list);
        item.pszText = const_cast<wchar_t*>(g_cards[index].name.c_str());
        item.lParam = static_cast<LPARAM>(index);
        const int list_index = ListView_InsertItem(g_list, &item);
        ListView_SetItemText(g_list, list_index, 1, const_cast<wchar_t*>(g_cards[index].id.c_str()));
        g_visible_cards.push_back(index);
    }
    SetWindowTextW(g_count, (std::to_wstring(g_visible_cards.size()) + L" 个结果").c_str());
    SetStatus(L"准备好了，选中喜欢的角色卡后即可下载。 ☆");
}

std::filesystem::path DefaultDownloadFolder() {
    PWSTR downloads = nullptr;
    std::filesystem::path folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloads))) {
        folder = downloads;
        CoTaskMemFree(downloads);
    } else {
        folder = std::filesystem::path(std::getenv("USERPROFILE")) / "Downloads";
    }
    folder /= "RPH Forum Cards";
    return folder;
}

std::wstring SafeFilename(const std::wstring& name) {
    std::wstring cleaned;
    for (const wchar_t character : name) {
        cleaned += wcschr(L"<>:\"/\\|?*", character) ? L'_' : character;
    }
    while (!cleaned.empty() && (cleaned.back() == L'.' || cleaned.back() == L' ')) cleaned.pop_back();
    if (cleaned.empty()) cleaned = L"character-card";
    if (cleaned.size() > 110) cleaned.resize(110);
    return cleaned + L".png";
}

std::filesystem::path UniquePath(const std::filesystem::path& folder, const std::wstring& filename) {
    std::filesystem::path target = folder / filename;
    unsigned int number = 2;
    while (std::filesystem::exists(target)) {
        target = folder / (std::filesystem::path(filename).stem().wstring() + L" (" + std::to_wstring(number++) + L")" + std::filesystem::path(filename).extension().wstring());
    }
    return target;
}

void DownloadCard(Card card) {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PostMessageW(g_window, WM_STATUS, 0, reinterpret_cast<LPARAM>(new std::wstring(L"正在下载：" + card.name)));
    const auto folder = g_download_folder;
    std::filesystem::create_directories(folder);
    const auto target = UniquePath(folder, SafeFilename(card.name));
    const std::wstring url = L"https://rphforum.zeabur.app/api/cards/" + card.id + L"/download/file";
    const HRESULT result = URLDownloadToFileW(nullptr, url.c_str(), target.c_str(), 0, nullptr);
    if (SUCCEEDED(com_result)) CoUninitialize();
    const std::wstring message = SUCCEEDED(result)
        ? L"下载完成：" + target.wstring()
        : L"下载失败，请检查网络连接或卡片链接。";
    PostMessageW(g_window, WM_DOWNLOAD_DONE, SUCCEEDED(result), reinterpret_cast<LPARAM>(new std::wstring(message)));
}

void StartDownload() {
    const int selected = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (selected == -1) {
        MessageBoxW(g_window, L"请先从列表中选一张角色卡。", L"RPH Card Library", MB_OK | MB_ICONINFORMATION);
        return;
    }
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = selected;
    ListView_GetItem(g_list, &item);
    const auto index = static_cast<size_t>(item.lParam);
    if (index >= g_cards.size()) return;
    std::thread(DownloadCard, g_cards[index]).detach();
}

void OpenDownloadFolder() {
    std::filesystem::create_directories(g_download_folder);
    ShellExecuteW(nullptr, L"open", g_download_folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ChooseDownloadFolder() {
    IFileDialog* dialog = nullptr;
    const HRESULT created = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileDialog, reinterpret_cast<void**>(&dialog));
    if (FAILED(created)) return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"选择角色卡保存目录");
    if (SUCCEEDED(dialog->Show(g_window))) {
        IShellItem* selected = nullptr;
        if (SUCCEEDED(dialog->GetResult(&selected))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(selected->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                g_download_folder = path;
                CoTaskMemFree(path);
                UpdateFolderLabel();
                SetStatus(L"保存目录已更新。 ✦");
            }
            selected->Release();
        }
    }
    dialog->Release();
}

void LayoutControls(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    const int margin = 24;
    const int width = client.right - margin * 2;
    MoveWindow(g_title, margin, 20, width, 34, TRUE);
    MoveWindow(g_subtitle, margin, 55, width, 22, TRUE);
    MoveWindow(g_search, margin, 98, width, 34, TRUE);
    MoveWindow(g_count, margin, 142, 130, 22, TRUE);
    MoveWindow(g_folder_label, margin + 135, 142, width - 135, 22, TRUE);
    MoveWindow(g_list, margin, 172, width, static_cast<int>(std::max<LONG>(120, client.bottom - 292)), TRUE);
    MoveWindow(g_status, margin, client.bottom - 106, width, 20, TRUE);
    MoveWindow(g_download, margin, client.bottom - 66, 168, 36, TRUE);
    MoveWindow(g_choose_folder, margin + 178, client.bottom - 66, 168, 36, TRUE);
    MoveWindow(g_open_folder, margin + 356, client.bottom - 66, 168, 36, TRUE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
        case WM_CREATE: {
            g_window = window;
            g_title = CreateWindowW(L"STATIC", L"RPH 角色卡收藏馆  ☆", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            g_subtitle = CreateWindowW(L"STATIC", L"挑选一张喜欢的角色卡，马上带回本地。", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            g_search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_SEARCH), nullptr, nullptr);
            SendMessageW(g_search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"输入角色卡名称进行搜索..."));
            g_count = CreateWindowW(L"STATIC", L"0 个结果", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            g_folder_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_LIST), nullptr, nullptr);
            ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            ListView_SetBkColor(g_list, RGB(255, 255, 255));
            ListView_SetTextBkColor(g_list, RGB(255, 255, 255));
            ListView_SetTextColor(g_list, COLOR_INK);
            LVCOLUMNW name_column{};
            name_column.mask = LVCF_TEXT | LVCF_WIDTH;
            name_column.pszText = const_cast<wchar_t*>(L"角色卡名称");
            name_column.cx = 520;
            ListView_InsertColumn(g_list, 0, &name_column);
            LVCOLUMNW id_column{};
            id_column.mask = LVCF_TEXT | LVCF_WIDTH;
            id_column.pszText = const_cast<wchar_t*>(L"卡片 ID");
            id_column.cx = 245;
            ListView_InsertColumn(g_list, 1, &id_column);
            SetWindowTheme(g_list, L"Explorer", nullptr);
            SetWindowTheme(g_search, L"Explorer", nullptr);
            g_status = CreateWindowW(L"STATIC", L"正在加载卡片目录...", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            g_download = CreateWindowW(L"BUTTON", L"下载选中卡片", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_DOWNLOAD), nullptr, nullptr);
            g_choose_folder = CreateWindowW(L"BUTTON", L"选择下载目录", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_CHOOSE_FOLDER), nullptr, nullptr);
            g_open_folder = CreateWindowW(L"BUTTON", L"打开下载目录", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_OPEN_FOLDER), nullptr, nullptr);
            SendMessageW(g_title, WM_SETFONT, reinterpret_cast<WPARAM>(g_title_font), TRUE);
            SendMessageW(g_subtitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_body_font), TRUE);
            SendMessageW(g_search, WM_SETFONT, reinterpret_cast<WPARAM>(g_body_font), TRUE);
            SendMessageW(g_count, WM_SETFONT, reinterpret_cast<WPARAM>(g_body_font), TRUE);
            SendMessageW(g_folder_label, WM_SETFONT, reinterpret_cast<WPARAM>(g_small_font), TRUE);
            SendMessageW(g_status, WM_SETFONT, reinterpret_cast<WPARAM>(g_small_font), TRUE);
            SendMessageW(g_download, WM_SETFONT, reinterpret_cast<WPARAM>(g_body_font), TRUE);
            SendMessageW(g_choose_folder, WM_SETFONT, reinterpret_cast<WPARAM>(g_body_font), TRUE);
            SendMessageW(g_open_folder, WM_SETFONT, reinterpret_cast<WPARAM>(g_body_font), TRUE);
            g_download_folder = DefaultDownloadFolder();
            UpdateFolderLabel();
            LayoutControls(window);
            if (!LoadCatalogResource()) SetStatus(L"内置目录加载失败。" );
            FillList();
            return 0;
        }
        case WM_SIZE:
            LayoutControls(window);
            return 0;
        case WM_COMMAND:
            if (LOWORD(w_param) == ID_SEARCH && HIWORD(w_param) == EN_CHANGE) FillList();
            if (LOWORD(w_param) == ID_DOWNLOAD) StartDownload();
            if (LOWORD(w_param) == ID_OPEN_FOLDER) OpenDownloadFolder();
            if (LOWORD(w_param) == ID_CHOOSE_FOLDER) ChooseDownloadFolder();
            return 0;
        case WM_NOTIFY: {
            const auto* notification = reinterpret_cast<NMHDR*>(l_param);
            if (notification->idFrom == ID_LIST && notification->code == NM_DBLCLK) StartDownload();
            if (notification->idFrom == ID_LIST && notification->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(l_param);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    draw->clrText = COLOR_INK;
                    draw->clrTextBk = (draw->nmcd.dwItemSpec % 2 == 0) ? RGB(255, 255, 255) : RGB(255, 246, 251);
                    return CDRF_NEWFONT;
                }
            }
            return 0;
        }
        case WM_DRAWITEM:
            if (w_param == ID_DOWNLOAD || w_param == ID_CHOOSE_FOLDER || w_param == ID_OPEN_FOLDER) {
                DrawAnimeButton(reinterpret_cast<DRAWITEMSTRUCT*>(l_param));
                return TRUE;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC device = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            RECT banner{16, 12, client.right - 16, 86};
            HBRUSH banner_brush = CreateSolidBrush(RGB(255, 225, 239));
            HPEN banner_pen = CreatePen(PS_SOLID, 1, RGB(250, 192, 216));
            const auto old_brush = SelectObject(device, banner_brush);
            const auto old_pen = SelectObject(device, banner_pen);
            RoundRect(device, banner.left, banner.top, banner.right, banner.bottom, 18, 18);
            SelectObject(device, old_brush);
            SelectObject(device, old_pen);
            DeleteObject(banner_brush);
            DeleteObject(banner_pen);
            HBRUSH sparkle = CreateSolidBrush(RGB(255, 198, 84));
            const auto old_sparkle = SelectObject(device, sparkle);
            Ellipse(device, client.right - 87, 29, client.right - 63, 53);
            Ellipse(device, client.right - 54, 48, client.right - 40, 62);
            SelectObject(device, old_sparkle);
            DeleteObject(sparkle);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            auto* device = reinterpret_cast<HDC>(w_param);
            SetBkMode(device, TRANSPARENT);
            if (reinterpret_cast<HWND>(l_param) == g_title) SetTextColor(device, RGB(158, 61, 104));
            else if (reinterpret_cast<HWND>(l_param) == g_count) SetTextColor(device, COLOR_PINK);
            else if (reinterpret_cast<HWND>(l_param) == g_subtitle || reinterpret_cast<HWND>(l_param) == g_folder_label || reinterpret_cast<HWND>(l_param) == g_status) SetTextColor(device, COLOR_MUTED);
            else SetTextColor(device, COLOR_INK);
            return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
        }
        case WM_STATUS: {
            auto* text = reinterpret_cast<std::wstring*>(l_param);
            SetStatus(*text);
            delete text;
            return 0;
        }
        case WM_DOWNLOAD_DONE: {
            auto* text = reinterpret_cast<std::wstring*>(l_param);
            SetStatus(*text);
            MessageBoxW(window, text->c_str(), L"RPH 角色卡收藏馆", w_param ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
            delete text;
            return 0;
        }
        case WM_DESTROY:
            DeleteObject(g_title_font);
            DeleteObject(g_body_font);
            DeleteObject(g_small_font);
            DeleteObject(g_button_font);
            DeleteObject(g_background);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int command_show) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_title_font = CreateFontW(27, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    g_body_font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    g_small_font = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    g_button_font = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    g_background = CreateSolidBrush(COLOR_APP_BACKGROUND);
    const wchar_t class_name[] = L"RPHCardDownloaderWindow";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = g_background;
    window_class.lpszClassName = class_name;
    RegisterClassW(&window_class);

    HWND window = CreateWindowExW(0, class_name, L"RPH 角色卡收藏馆 ☆", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 680, nullptr, nullptr, instance, nullptr);
    ShowWindow(window, command_show);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return 0;
}
