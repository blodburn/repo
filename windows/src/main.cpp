#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <richedit.h>
#include <shlobj.h>
#include <sqlite3.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

static constexpr UINT IDM_NEW = 1001;
static constexpr UINT IDM_OPEN = 1002;
static constexpr UINT IDM_SAVE = 1003;
static constexpr UINT IDM_UNDO = 1004;
static constexpr UINT IDM_REDO = 1005;
static constexpr UINT IDM_CREATE_OBJECT = 1006;
static constexpr UINT TIMER_AUTOSAVE = 1;
static constexpr UINT TIMER_PARSE = 2;

static HWND gMain = nullptr;
static HWND gObjectList = nullptr;
static HWND gEditor = nullptr;
static HWND gInfo = nullptr;
static HWND gProjectLabel = nullptr;
static HWND gStatus = nullptr;
static HFONT gFont = nullptr;
static sqlite3* gDb = nullptr;
static fs::path gProject;
static bool gLoading = false;
static std::wstring gLastRevision;

struct StoryObject {
    sqlite3_int64 id{};
    std::wstring type;
    std::wstring name;
    int mentions{};
};

static std::vector<StoryObject> gObjects;

static std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

static std::wstring WindowText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text((size_t)len, L'\0');
    if (len) GetWindowTextW(hwnd, text.data(), len + 1);
    return text;
}

static void SetStatus(const std::wstring& s) {
    SetWindowTextW(gStatus, s.c_str());
}

static bool Exec(const char* sql) {
    char* error = nullptr;
    int rc = sqlite3_exec(gDb, sql, nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::wstring msg = L"SQLite: " + Wide(error ? error : "error");
        SetStatus(msg);
        sqlite3_free(error);
        return false;
    }
    return true;
}

static void CloseDb() {
    if (gDb) sqlite3_close(gDb);
    gDb = nullptr;
}

static bool OpenDb(const fs::path& path) {
    CloseDb();
    if (sqlite3_open(Utf8(path.wstring()).c_str(), &gDb) != SQLITE_OK) {
        SetStatus(L"Could not open project.sqlite");
        CloseDb();
        return false;
    }
    Exec("PRAGMA journal_mode=WAL;");
    Exec("PRAGMA synchronous=NORMAL;");
    Exec("PRAGMA foreign_keys=ON;");
    return
      Exec("CREATE TABLE IF NOT EXISTS objects("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "type TEXT NOT NULL DEFAULT 'character',"
           "name TEXT NOT NULL UNIQUE,"
           "deleted INTEGER NOT NULL DEFAULT 0,"
           "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
           "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);")
      && Exec("CREATE TABLE IF NOT EXISTS mentions("
              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
              "document_id TEXT NOT NULL,"
              "object_id INTEGER NOT NULL REFERENCES objects(id) ON DELETE CASCADE,"
              "char_offset INTEGER NOT NULL);")
      && Exec("CREATE INDEX IF NOT EXISTS idx_mentions_object ON mentions(object_id);")
      && Exec("CREATE TABLE IF NOT EXISTS revisions("
              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
              "document_id TEXT NOT NULL,"
              "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
              "reason TEXT NOT NULL DEFAULT 'autosave',"
              "content TEXT NOT NULL);");
}

static sqlite3_int64 EnsureObject(const std::wstring& rawName, const std::wstring& rawType = L"character") {
    if (!gDb) return 0;
    std::wstring name = rawName;
    while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    if (name.empty()) return 0;

    std::string u8name = Utf8(name);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(gDb, "SELECT id FROM objects WHERE name=? COLLATE NOCASE AND deleted=0 LIMIT 1", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, u8name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto id = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return id;
    }
    sqlite3_finalize(stmt);

    std::wstring type = rawType.empty() ? L"character" : rawType;
    std::string u8type = Utf8(type);
    sqlite3_prepare_v2(gDb, "INSERT INTO objects(type,name) VALUES(?,?)", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, u8type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, u8name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(gDb);
}

static std::wstring NormalizeType(std::wstring t) {
    while (!t.empty() && iswspace(t.front())) t.erase(t.begin());
    while (!t.empty() && iswspace(t.back())) t.pop_back();
    if (t.empty() || t == L"인물" || t == L"character") return L"character";
    if (t == L"장소" || t == L"location") return L"location";
    if (t == L"사건" || t == L"event") return L"event";
    if (t == L"조직" || t == L"organization") return L"organization";
    if (t == L"물건" || t == L"item") return L"item";
    return t;
}

static void ScanExplicitObjects(const std::wstring& text) {
    size_t pos = 0;
    while (true) {
        size_t a = text.find(L"[[", pos);
        if (a == std::wstring::npos) break;
        size_t b = text.find(L"]]", a + 2);
        if (b == std::wstring::npos) break;
        std::wstring body = text.substr(a + 2, b - a - 2);
        size_t pipe = body.find(L'|');
        if (pipe != std::wstring::npos) body = body.substr(0, pipe);
        std::wstring type = L"character";
        std::wstring name = body;
        size_t colon = body.find(L':');
        if (colon != std::wstring::npos) {
            type = NormalizeType(body.substr(0, colon));
            name = body.substr(colon + 1);
        }
        EnsureObject(name, type);
        pos = b + 2;
    }
}

static void LoadObjects() {
    gObjects.clear();
    if (!gDb) return;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(
        gDb,
        "SELECT o.id,o.type,o.name,COUNT(m.id) "
        "FROM objects o LEFT JOIN mentions m ON m.object_id=o.id "
        "WHERE o.deleted=0 GROUP BY o.id,o.type,o.name "
        "ORDER BY o.name COLLATE NOCASE",
        -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        StoryObject o;
        o.id = sqlite3_column_int64(stmt, 0);
        o.type = Wide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        o.name = Wide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        o.mentions = sqlite3_column_int(stmt, 3);
        gObjects.push_back(std::move(o));
    }
    sqlite3_finalize(stmt);
}

static void RefreshObjectList() {
    SendMessageW(gObjectList, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < gObjects.size(); ++i) {
        const auto& o = gObjects[i];
        std::wstring label = o.name + L"  [" + o.type + L"]  " + std::to_wstring(o.mentions);
        LRESULT row = SendMessageW(gObjectList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
        SendMessageW(gObjectList, LB_SETITEMDATA, row, (LPARAM)i);
    }
    if (!gObjects.empty()) SendMessageW(gObjectList, LB_SETCURSEL, 0, 0);
}

static void RebuildMentions(const std::wstring& text) {
    if (!gDb) return;
    LoadObjects();
    Exec("BEGIN IMMEDIATE;");
    sqlite3_stmt* del = nullptr;
    sqlite3_prepare_v2(gDb, "DELETE FROM mentions WHERE document_id='script/main.txt'", -1, &del, nullptr);
    sqlite3_step(del);
    sqlite3_finalize(del);

    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(gDb, "INSERT INTO mentions(document_id,object_id,char_offset) VALUES('script/main.txt',?,?)", -1, &ins, nullptr);
    for (const auto& o : gObjects) {
        if (o.name.empty()) continue;
        size_t pos = 0;
        while ((pos = text.find(o.name, pos)) != std::wstring::npos) {
            sqlite3_reset(ins);
            sqlite3_bind_int64(ins, 1, o.id);
            sqlite3_bind_int64(ins, 2, (sqlite3_int64)pos);
            sqlite3_step(ins);
            pos += std::max<size_t>(1, o.name.size());
        }
    }
    sqlite3_finalize(ins);
    Exec("COMMIT;");
    LoadObjects();
}

static void ParseDocument() {
    if (!gDb || gProject.empty()) return;
    std::wstring text = WindowText(gEditor);
    ScanExplicitObjects(text);
    RebuildMentions(text);
    RefreshObjectList();
}

static fs::path ScriptPath() {
    return gProject / L"script" / L"main.txt";
}

static bool WriteUtf8File(const fs::path& path, const std::wstring& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    std::string u8 = Utf8(text);
    out.write(u8.data(), (std::streamsize)u8.size());
    return out.good();
}

static std::wstring ReadUtf8File(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Wide(data);
}

static void AddRevision(const std::wstring& text, const char* reason) {
    if (!gDb || text == gLastRevision) return;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(gDb, "INSERT INTO revisions(document_id,reason,content) VALUES('script/main.txt',?,?)", -1, &stmt, nullptr);
    std::string u8 = Utf8(text);
    sqlite3_bind_text(stmt, 1, reason, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, u8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    Exec("DELETE FROM revisions WHERE document_id='script/main.txt' AND id NOT IN("
         "SELECT id FROM revisions WHERE document_id='script/main.txt' ORDER BY id DESC LIMIT 500);");
    gLastRevision = text;
}

static void SaveProject(const char* reason = "manual") {
    if (gProject.empty()) return;
    std::wstring text = WindowText(gEditor);
    if (!WriteUtf8File(ScriptPath(), text)) {
        SetStatus(L"Save failed");
        return;
    }
    AddRevision(text, reason);
    SetStatus(L"Saved");
}

static std::wstring PickFolder(HWND owner, const wchar_t* title) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return {};
    wchar_t path[MAX_PATH]{};
    bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    return ok ? std::wstring(path) : std::wstring();
}

static void OpenProjectFolder(const std::wstring& folder, bool create) {
    if (folder.empty()) return;
    if (!gProject.empty()) SaveProject("close");

    gProject = fs::path(folder);
    if (create) fs::create_directories(gProject);
    fs::create_directories(gProject / L"script");

    if (!OpenDb(gProject / L"project.sqlite")) return;

    std::wstring text;
    if (fs::exists(ScriptPath())) {
        text = ReadUtf8File(ScriptPath());
    } else {
        text =
            L"# 첫 장면\r\n\r\n"
            L"## 상황 설명을 입력하세요. ##\r\n\r\n"
            L"[[김철수]]\r\n"
            L"@@ 대사를 입력하세요. @@\r\n\r\n"
            L"₩₩ 나레이션을 입력하세요. ₩₩\r\n";
    }

    gLoading = true;
    SetWindowTextW(gEditor, text.c_str());
    gLoading = false;
    SetWindowTextW(gProjectLabel, gProject.wstring().c_str());

    ParseDocument();
    gLastRevision = text;
    SaveProject("open");
    SetFocus(gEditor);
    SetStatus(L"Project loaded");
}

static std::wstring SelectedOrCurrentWord() {
    CHARRANGE range{};
    SendMessageW(gEditor, EM_EXGETSEL, 0, (LPARAM)&range);
    std::wstring text = WindowText(gEditor);
    size_t a = (size_t)std::max<LONG>(0, range.cpMin);
    size_t b = (size_t)std::max<LONG>(0, range.cpMax);
    if (b > a && b <= text.size()) return text.substr(a, b - a);

    size_t caret = std::min(a, text.size());
    size_t left = caret;
    while (left > 0 && !iswspace(text[left - 1])) --left;
    size_t right = caret;
    while (right < text.size() && !iswspace(text[right])) ++right;
    return text.substr(left, right - left);
}

static void CreateObjectFromSelection() {
    if (!gDb) {
        SetStatus(L"Open a project first");
        return;
    }
    std::wstring name = SelectedOrCurrentWord();
    auto eraseAll = [&](const std::wstring& token) {
        size_t p = 0;
        while ((p = name.find(token, p)) != std::wstring::npos) name.erase(p, token.size());
    };
    eraseAll(L"[[");
    eraseAll(L"]]");
    while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    if (name.empty()) {
        SetStatus(L"Select an object name first");
        return;
    }
    if (EnsureObject(name) > 0) {
        ParseDocument();
        SetStatus(L"Object registered: " + name);
    }
    SetFocus(gEditor);
}

static void ShowSelectedObject() {
    int row = (int)SendMessageW(gObjectList, LB_GETCURSEL, 0, 0);
    if (row == LB_ERR) {
        SetWindowTextW(gInfo, L"");
        return;
    }
    size_t idx = (size_t)SendMessageW(gObjectList, LB_GETITEMDATA, row, 0);
    if (idx >= gObjects.size()) return;
    const auto& o = gObjects[idx];
    std::wstring info =
        L"Name\r\n" + o.name +
        L"\r\n\r\nType\r\n" + o.type +
        L"\r\n\r\nMentions\r\n" + std::to_wstring(o.mentions);
    SetWindowTextW(gInfo, info.c_str());
}

static void Layout(HWND hwnd) {
    RECT r{};
    GetClientRect(hwnd, &r);
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    int top = 34;
    int bottom = 24;
    int left = 230;
    int right = 290;
    int gap = 1;
    int center = std::max(200, w - left - right - gap * 2);

    MoveWindow(gProjectLabel, 8, 5, w - 16, 24, TRUE);
    MoveWindow(gObjectList, 0, top, left, h - top - bottom, TRUE);
    MoveWindow(gEditor, left + gap, top, center, h - top - bottom, TRUE);
    MoveWindow(gInfo, left + gap + center + gap, top, right, h - top - bottom, TRUE);
    MoveWindow(gStatus, 8, h - bottom + 2, w - 16, bottom - 4, TRUE);
}

static void BuildMenu(HWND hwnd) {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU edit = CreatePopupMenu();

    AppendMenuW(file, MF_STRING, IDM_NEW, L"&New Project\tCtrl+N");
    AppendMenuW(file, MF_STRING, IDM_OPEN, L"&Open Project\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_SAVE, L"&Save\tCtrl+S");

    AppendMenuW(edit, MF_STRING, IDM_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, IDM_REDO, L"&Redo\tCtrl+U");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, IDM_CREATE_OBJECT, L"Create Object\tCtrl+Shift+O");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    SetMenu(hwnd, bar);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        gMain = hwnd;
        BuildMenu(hwnd);

        gFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        gProjectLabel = CreateWindowExW(0, L"STATIC", L"No project",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 24, hwnd, nullptr, nullptr, nullptr);

        gObjectList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 100, 100, hwnd, (HMENU)2001, nullptr, nullptr);

        gEditor = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
            0, 0, 100, 100, hwnd, (HMENU)2002, nullptr, nullptr);

        gInfo = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 100, 100, hwnd, (HMENU)2003, nullptr, nullptr);

        gStatus = CreateWindowExW(0, L"STATIC", L"Ready",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 24, hwnd, nullptr, nullptr, nullptr);

        SendMessageW(gObjectList, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gEditor, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gInfo, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gProjectLabel, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gStatus, WM_SETFONT, (WPARAM)gFont, TRUE);

        SendMessageW(gEditor, EM_SETUNDOLIMIT, 10000, 0);
        return 0;
    }
    case WM_SIZE:
        Layout(hwnd);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_AUTOSAVE) {
            KillTimer(hwnd, TIMER_AUTOSAVE);
            SaveProject("autosave");
        } else if (wParam == TIMER_PARSE) {
            KillTimer(hwnd, TIMER_PARSE);
            ParseDocument();
        }
        return 0;

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if ((HWND)lParam == gEditor && code == EN_CHANGE && !gLoading) {
            SetTimer(hwnd, TIMER_AUTOSAVE, 800, nullptr);
            SetTimer(hwnd, TIMER_PARSE, 180, nullptr);
        }
        if ((HWND)lParam == gObjectList && code == LBN_SELCHANGE) {
            ShowSelectedObject();
        }

        switch (id) {
        case IDM_NEW: {
            auto folder = PickFolder(hwnd, L"Choose or create the project folder");
            if (!folder.empty()) OpenProjectFolder(folder, true);
            return 0;
        }
        case IDM_OPEN: {
            auto folder = PickFolder(hwnd, L"Choose a repo project folder");
            if (!folder.empty()) OpenProjectFolder(folder, false);
            return 0;
        }
        case IDM_SAVE:
            SaveProject("manual");
            return 0;
        case IDM_UNDO:
            SendMessageW(gEditor, EM_UNDO, 0, 0);
            return 0;
        case IDM_REDO:
            SendMessageW(gEditor, EM_REDO, 0, 0);
            return 0;
        case IDM_CREATE_OBJECT:
            CreateObjectFromSelection();
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            SetFocus(gEditor);
            return 0;
        }
        break;

    case WM_CLOSE:
        SaveProject("close");
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        CloseDb();
        if (gFont) DeleteObject(gFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadLibraryW(L"Msftedit.dll");

    const wchar_t CLASS_NAME[] = L"RepoPortableWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"repo portable 0.1.2",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        CoUninitialize();
        return 1;
    }

    ACCEL accel[] = {
        {FVIRTKEY | FCONTROL, 'N', IDM_NEW},
        {FVIRTKEY | FCONTROL, 'O', IDM_OPEN},
        {FVIRTKEY | FCONTROL, 'S', IDM_SAVE},
        {FVIRTKEY | FCONTROL, 'Z', IDM_UNDO},
        {FVIRTKEY | FCONTROL, 'U', IDM_REDO},
        {FVIRTKEY | FCONTROL | FSHIFT, 'O', IDM_CREATE_OBJECT}
    };
    HACCEL hAccel = CreateAcceleratorTableW(accel, ARRAYSIZE(accel));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    SetFocus(gEditor);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!TranslateAcceleratorW(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    DestroyAcceleratorTable(hAccel);
    CoUninitialize();
    return (int)msg.wParam;
}
