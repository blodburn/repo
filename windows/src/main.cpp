#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <richedit.h>
#include <commdlg.h>
#include "Export.h"
#include <shlobj.h>
#include <sqlite3.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <cwctype>

namespace fs = std::filesystem;

static constexpr UINT IDM_NEW = 1001;
static constexpr UINT IDM_OPEN = 1002;
static constexpr UINT IDM_SAVE = 1003;
static constexpr UINT IDM_UNDO = 1004;
static constexpr UINT IDM_REDO = 1005;
static constexpr UINT IDM_CREATE_OBJECT = 1006;
static constexpr UINT IDM_HELP = 1007;
static constexpr UINT IDM_EXPORT_DOCX = 1008;
static constexpr UINT IDM_EXPORT_HWPX = 1009;
static constexpr UINT IDM_EXPORT_PDF = 1010;
static constexpr UINT IDM_NEXT_MENTION = 1011;
static constexpr UINT IDM_HISTORY_RESTORE = 1012;
static constexpr UINT IDM_PREVIEW = 1013;
static constexpr UINT IDM_TITLE = 1014;
static constexpr UINT TIMER_AUTOSAVE = 1;
static constexpr UINT TIMER_PARSE = 2;

static HWND gMain = nullptr;
static HWND gObjectList = nullptr;
static HWND gEditor = nullptr;
static HWND gInfo = nullptr;
static HWND gProjectLabel = nullptr;
static HWND gTitleLabel = nullptr;
static HWND gTitleEdit = nullptr;
static HWND gObjectHeader = nullptr;
static HWND gInfoHeader = nullptr;
static HWND gPreview = nullptr;
static bool gPreviewVisible = false;
static HWND gStatus = nullptr;
static HFONT gFont = nullptr;
static sqlite3* gDb = nullptr;
static fs::path gProject;
static bool gLoading = false;
static std::wstring gLastRevision;
static std::wstring gTitle;
static const wchar_t* HELP_TEXT =
    L"repo  |  빠른 시작\r\n\r\n"
    L"1. Ctrl+N : 새 작품 프로젝트 생성\r\n"
    L"2. 상단 작품 제목 입력 후 원고 작성\r\n"
    L"3. [[김철수]] : 등장인물 등록\r\n"
    L"   [[장소:서울역]] : 장소 등록\r\n"
    L"   이후 김철수, 서울역을 일반 문장으로 입력해도 자동 인식\r\n\r\n"
    L"문법:\r\n"
    L" # 병원 복도 - 밤  : 장면\r\n"
    L" @@ 안녕하세요 @@  : 대사\r\n"
    L" ## 문이 열린다 ##  : 상황/행동\r\n"
    L" ₩₩ 그날 밤... ₩₩  : 나레이션\r\n"
    L" // 작성자 메모 : 최종 출력 제외\r\n\r\n"
    L"Ctrl+S 저장  /  Ctrl+Z 실행 취소  /  Ctrl+U 다시 실행\r\n"
    L"Ctrl+Shift+O 단어/선택 영역을 오브젝트로 등록\r\n"
    L"Ctrl+G 선택 오브젝트의 다음 등장 위치\r\n"
    L"Ctrl+Shift+P 서식 미리보기  /  Esc 본문 복귀\r\n"
    L"Ctrl+Shift+H 이전 저장본 복원\r\n"
    L"Ctrl+T 작품 제목 수정  /  F1 사용법\r\n\r\n"
    L"File > Export : Word(.docx), PDF, HWPX(실험)\r\n"
    L"원본: script\\main.txt (UTF-8)\r\n"
    L"오브젝트/자동 저장 이력: project.sqlite\r\n";

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
    std::wstring text((size_t)len + 1, L'\0');
    if (len) GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize((size_t)len);
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
      Exec("CREATE TABLE IF NOT EXISTS project_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);")
      && Exec("CREATE TABLE IF NOT EXISTS objects("
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

static void ShowSelectedObject();
static void RefreshObjectList() {
    sqlite3_int64 selectedID = 0;
    int previous = (int)SendMessageW(gObjectList, LB_GETCURSEL, 0, 0);
    if (previous != LB_ERR) {
        size_t idx = (size_t)SendMessageW(gObjectList, LB_GETITEMDATA, previous, 0);
        if (idx < gObjects.size()) selectedID = gObjects[idx].id;
    }
    SendMessageW(gObjectList, LB_RESETCONTENT, 0, 0);
    int restore = -1;
    for (size_t i = 0; i < gObjects.size(); ++i) {
        const auto& o = gObjects[i];
        std::wstring label = o.name + L"  [" + o.type + L"]  " + std::to_wstring(o.mentions);
        LRESULT row = SendMessageW(gObjectList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
        SendMessageW(gObjectList, LB_SETITEMDATA, row, (LPARAM)i);
        if (o.id == selectedID) restore = (int)row;
    }
    if (!gObjects.empty()) SendMessageW(gObjectList, LB_SETCURSEL, restore >= 0 ? restore : 0, 0);
    ShowSelectedObject();
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

static void UpdatePreview();

static void ParseDocument() {
    if (!gDb || gProject.empty()) return;
    std::wstring text = WindowText(gEditor);
    ScanExplicitObjects(text);
    RebuildMentions(text);
    RefreshObjectList();
    if (gPreviewVisible) UpdatePreview();
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

static void SaveTitle() {
    if (!gDb || gProject.empty()) return;
    gTitle = WindowText(gTitleEdit);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(gDb, "INSERT INTO project_meta(key,value) VALUES('title',?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value", -1, &stmt, nullptr) != SQLITE_OK) return;
    std::string value = Utf8(gTitle);
    sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    std::wstring caption = (gTitle.empty() ? L"repo" : gTitle) + L" - repo 0.2.0";
    SetWindowTextW(gMain, caption.c_str());
}
static std::wstring LoadTitle() {
    if (!gDb) return {};
    sqlite3_stmt* stmt = nullptr;
    std::wstring result;
    if (sqlite3_prepare_v2(gDb, "SELECT value FROM project_meta WHERE key='title'", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (value) result = Wide(value);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}
static void SaveProject(const char* reason = "manual") {
    if (gProject.empty()) return;
    SaveTitle();
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
    gTitle = LoadTitle();
    if (gTitle.empty()) gTitle = gProject.filename().wstring();

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
    SendMessageW(gEditor, EM_SETREADONLY, FALSE, 0);
    SetWindowTextW(gEditor, text.c_str());
    SendMessageW(gTitleEdit, EM_SETREADONLY, FALSE, 0);
    SetWindowTextW(gTitleEdit, gTitle.c_str());
    gLoading = false;
    SetWindowTextW(gProjectLabel, gProject.wstring().c_str());
    SaveTitle();

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
    int top = 76;
    int bottom = 24;
    int left = 240;
    int right = 315;
    int gap = 1;
    int center = std::max(200, w - left - right - gap * 2);
    int paneY = top + 27;
    int paneHeight = std::max(10, h - paneY - bottom);

    MoveWindow(gTitleLabel, 8, 7, 84, 24, TRUE);
    MoveWindow(gTitleEdit, 92, 4, std::min(390, std::max(120, w / 3)), 27, TRUE);
    MoveWindow(gProjectLabel, 8, 39, w - 16, 24, TRUE);
    MoveWindow(gObjectHeader, 6, top, left - 10, 23, TRUE);
    MoveWindow(gInfoHeader, left + gap + center + 9, top, right - 12, 23, TRUE);
    MoveWindow(gObjectList, 0, paneY, left, paneHeight, TRUE);
    MoveWindow(gEditor, left + gap, paneY, center, paneHeight, TRUE);
    MoveWindow(gPreview, left + gap, paneY, center, paneHeight, TRUE);
    MoveWindow(gInfo, left + gap + center + gap, paneY, right, paneHeight, TRUE);
    MoveWindow(gStatus, 8, h - bottom + 2, w - 16, bottom - 4, TRUE);
}

static void BuildMenu(HWND hwnd) {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU exportMenu = CreatePopupMenu();
    HMENU edit = CreatePopupMenu();
    HMENU help = CreatePopupMenu();

    AppendMenuW(file, MF_STRING, IDM_NEW, L"&New Project\tCtrl+N");
    AppendMenuW(file, MF_STRING, IDM_OPEN, L"&Open Project\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(exportMenu, MF_STRING, IDM_EXPORT_DOCX, L"Word (.docx)");
    AppendMenuW(exportMenu, MF_STRING, IDM_EXPORT_HWPX, L"Hancom (.hwpx) - Experimental");
    AppendMenuW(exportMenu, MF_STRING, IDM_EXPORT_PDF, L"PDF (.pdf)");
    AppendMenuW(file, MF_POPUP, (UINT_PTR)exportMenu, L"&Export");

    AppendMenuW(edit, MF_STRING, IDM_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, IDM_REDO, L"&Redo\tCtrl+U");
    AppendMenuW(edit, MF_STRING, IDM_TITLE, L"Edit Title\tCtrl+T");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, IDM_CREATE_OBJECT, L"Create Object\tCtrl+Shift+O");
    AppendMenuW(edit, MF_STRING, IDM_NEXT_MENTION, L"Next Object Occurrence\tCtrl+G");
    AppendMenuW(edit, MF_STRING, IDM_PREVIEW, L"Toggle Screenplay Preview\tCtrl+Shift+P");
    AppendMenuW(edit, MF_STRING, IDM_HISTORY_RESTORE, L"Restore Earlier Saved Version\tCtrl+Shift+H");
    AppendMenuW(help, MF_STRING, IDM_HELP, L"Getting Started\tF1");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    SetMenu(hwnd, bar);
}

static void UpdatePreview() {
    if (!gPreview || gProject.empty()) return;
    std::vector<std::wstring> names;
    for (const auto& o : gObjects)
        if (o.type == L"character") names.push_back(o.name);
    auto paragraphs = RepoExport::Parse(WindowText(gEditor), WindowText(gTitleEdit), names);
    struct Range { LONG begin, end; RepoExport::Paragraph::Kind kind; };
    std::vector<Range> ranges;
    std::wstring display;
    for (const auto& para : paragraphs) {
        LONG start = (LONG)display.size();
        display += para.text;
        LONG end = (LONG)display.size();
        display += L"\r\n";
        ranges.push_back({start, end, para.kind});
    }
    SetWindowTextW(gPreview, display.c_str());
    for (const auto& item : ranges) {
        if (item.end <= item.begin) continue;
        CHARRANGE range{item.begin, item.end};
        SendMessageW(gPreview, EM_EXSETSEL, 0, (LPARAM)&range);
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_BOLD;
        cf.crTextColor = RGB(40, 40, 40);
        cf.dwEffects = 0;
        if (item.kind == RepoExport::Paragraph::Kind::Scene ||
            item.kind == RepoExport::Paragraph::Kind::Character ||
            item.kind == RepoExport::Paragraph::Kind::Title) cf.dwEffects |= CFE_BOLD;
        if (item.kind == RepoExport::Paragraph::Kind::Dialogue) cf.crTextColor = RGB(36, 69, 117);
        if (item.kind == RepoExport::Paragraph::Kind::Description) cf.crTextColor = RGB(65, 75, 65);
        if (item.kind == RepoExport::Paragraph::Kind::Narration) cf.crTextColor = RGB(104, 48, 112);
        SendMessageW(gPreview, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        PARAFORMAT2 pf{};
        pf.cbSize = sizeof(pf);
        pf.dwMask = PFM_STARTINDENT | PFM_ALIGNMENT;
        pf.wAlignment = PFA_LEFT;
        pf.dxStartIndent = 100;
        switch (item.kind) {
        case RepoExport::Paragraph::Kind::Character: pf.dxStartIndent = 2500; break;
        case RepoExport::Paragraph::Kind::Dialogue: pf.dxStartIndent = 1700; break;
        case RepoExport::Paragraph::Kind::Narration: pf.dxStartIndent = 850; break;
        case RepoExport::Paragraph::Kind::Title: pf.wAlignment = PFA_CENTER; break;
        default: break;
        }
        SendMessageW(gPreview, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
    CHARRANGE cursor{0,0};
    SendMessageW(gPreview, EM_EXSETSEL, 0, (LPARAM)&cursor);
    SendMessageW(gPreview, EM_SCROLL, SB_TOP, 0);
}

static void RestoreEarlierRevision() {
    if (!gDb || gProject.empty()) {
        SetStatus(L"먼저 프로젝트를 여세요.");
        return;
    }
    // Save current edits as their own restoration point before returning to an older one.
    SaveProject("before-restore");
    sqlite3_stmt* stmt = nullptr;
    std::wstring previous;
    if (sqlite3_prepare_v2(gDb, "SELECT content FROM revisions WHERE document_id='script/main.txt' "
        "ORDER BY id DESC LIMIT 1 OFFSET 1", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (txt) previous = Wide(txt);
        }
    }
    sqlite3_finalize(stmt);
    if (previous.empty()) {
        SetStatus(L"복구할 이전 저장본이 없습니다.");
        return;
    }
    if (MessageBoxW(gMain, L"이전 저장본으로 복원할까요?\n현재 원고는 복구 기록에 보존됩니다.",
        L"이전 저장본 복원", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES) return;
    gLoading = true;
    SetWindowTextW(gEditor, previous.c_str());
    gLoading = false;
    SaveProject("restore");
    ParseDocument();
    SetFocus(gEditor);
}

static void ExportFromEditor(UINT action) {
    if (gProject.empty()) {
        MessageBoxW(gMain, L"먼저 Ctrl+N으로 작품을 만들거나 Ctrl+O로 열어주세요.",
            L"프로젝트 없음", MB_OK | MB_ICONINFORMATION);
        return;
    }
    RepoExport::Format type = RepoExport::Format::WordDocx;
    const wchar_t* ext = L"docx";
    const wchar_t* filter = L"Word Document (*.docx)\0*.docx\0All Files (*.*)\0*.*\0\0";
    if (action == IDM_EXPORT_PDF) {
        type = RepoExport::Format::Pdf; ext = L"pdf";
        filter = L"PDF Document (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0\0";
    }
    if (action == IDM_EXPORT_HWPX) {
        type = RepoExport::Format::HancomHwpx; ext = L"hwpx";
        filter = L"Hancom HWPX (*.hwpx)\0*.hwpx\0All Files (*.*)\0*.*\0\0";
    }
    std::wstring name = WindowText(gTitleEdit);
    if (name.empty()) name = L"script";
    for (wchar_t& ch : name) {
        if (wcschr(L"<>:/\\|?*\"", ch)) ch = L'_';
    }
    std::wstring suggested = name + L"." + ext;
    wchar_t path[32768]{};
    wcsncpy_s(path, suggested.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = gMain;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = ARRAYSIZE(path);
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    std::wstring initialDir = gProject.wstring();
    ofn.lpstrInitialDir = initialDir.c_str();
    if (!GetSaveFileNameW(&ofn)) return;

    std::vector<std::wstring> names;
    for (const auto& o : gObjects)
        if (o.type == L"character") names.push_back(o.name);
    std::wstring error;
    if (!RepoExport::Write(fs::path(path), type, WindowText(gEditor),
                           WindowText(gTitleEdit), names, error)) {
        MessageBoxW(gMain, error.c_str(), L"내보내기 실패", MB_OK | MB_ICONERROR);
        return;
    }
    SetStatus(L"내보내기 완료: " + std::wstring(path));
    if (type == RepoExport::Format::HancomHwpx)
        MessageBoxW(gMain, L"HWPX는 현재 실험적 내보내기입니다. 한컴오피스에서 파일 열림/서식을 확인하세요.",
            L"HWPX 테스트 안내", MB_OK | MB_ICONINFORMATION);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        gMain = hwnd;
        BuildMenu(hwnd);

        gFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        gTitleLabel = CreateWindowExW(0, L"STATIC", L"작품 제목:",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 24, hwnd, nullptr, nullptr, nullptr);
        gTitleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, 0, 0, 300, 27,
            hwnd, (HMENU)2004, nullptr, nullptr);
        gProjectLabel = CreateWindowExW(0, L"STATIC", L"프로젝트가 없습니다. Ctrl+N: 새 작품  /  Ctrl+O: 열기  /  F1: 도움말",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 24, hwnd, nullptr, nullptr, nullptr);
        gObjectHeader = CreateWindowExW(0, L"STATIC", L"  오브젝트  |  Ctrl+Shift+O 등록",
            WS_CHILD | WS_VISIBLE, 0,0,100,24,hwnd,nullptr,nullptr,nullptr);
        gInfoHeader = CreateWindowExW(0, L"STATIC", L"  오브젝트 정보 / 도움말",
            WS_CHILD | WS_VISIBLE, 0,0,100,24,hwnd,nullptr,nullptr,nullptr);

        gObjectList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 100, 100, hwnd, (HMENU)2001, nullptr, nullptr);

        gEditor = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
            0, 0, 100, 100, hwnd, (HMENU)2002, nullptr, nullptr);

        gInfo = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 100, 100, hwnd, (HMENU)2003, nullptr, nullptr);
        gPreview = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 100, 100, hwnd, (HMENU)2005, nullptr, nullptr);

        gStatus = CreateWindowExW(0, L"STATIC", L"Ready",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 24, hwnd, nullptr, nullptr, nullptr);

        SendMessageW(gObjectList, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gEditor, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gInfo, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gProjectLabel, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gStatus, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gTitleLabel, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gTitleEdit, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gObjectHeader, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gInfoHeader, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(gPreview, WM_SETFONT, (WPARAM)gFont, TRUE);
        SetWindowTextW(gInfo, HELP_TEXT);
        SetWindowTextW(gEditor, L"Ctrl+N 새 작품 만들기\r\nCtrl+O 기존 작품 열기\r\nF1 전체 사용법\r\n\r\n[이 화면은 원고로 저장되지 않습니다.]");
        SendMessageW(gEditor, EM_SETREADONLY, TRUE, 0);

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

        if ((HWND)lParam == gTitleEdit && code == EN_CHANGE && !gLoading && !gProject.empty()) {
            SetTimer(hwnd, TIMER_AUTOSAVE, 650, nullptr);
        }
        if ((HWND)lParam == gEditor && code == EN_CHANGE && !gLoading && !gProject.empty()) {
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
        case IDM_HELP:
            SetWindowTextW(gInfo, HELP_TEXT);
            if (gProject.empty()) MessageBoxW(hwnd, HELP_TEXT, L"repo 사용법", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_TITLE:
            if (!gProject.empty()) SetFocus(gTitleEdit);
            return 0;
        case IDM_PREVIEW:
            if (!gProject.empty()) {
                gPreviewVisible = !gPreviewVisible;
                if (gPreviewVisible) UpdatePreview();
                ShowWindow(gEditor, gPreviewVisible ? SW_HIDE : SW_SHOW);
                ShowWindow(gPreview, gPreviewVisible ? SW_SHOW : SW_HIDE);
                SetFocus(gPreviewVisible ? gPreview : gEditor);
            }
            return 0;
        case IDM_NEXT_MENTION: {
            if (gObjects.empty()) return 0;
            int row = (int)SendMessageW(gObjectList, LB_GETCURSEL, 0, 0);
            if (row == LB_ERR || (size_t)row >= gObjects.size()) return 0;
            const auto& name = gObjects[(size_t)row].name;
            std::wstring body = WindowText(gEditor);
            CHARRANGE selection{};
            SendMessageW(gEditor, EM_EXGETSEL, 0, (LPARAM)&selection);
            size_t next = body.find(name, size_t(selection.cpMax));
            if (next == std::wstring::npos) next = body.find(name);
            if (next != std::wstring::npos) {
                CHARRANGE found{(LONG)next, (LONG)(next + name.size())};
                SendMessageW(gEditor, EM_EXSETSEL, 0, (LPARAM)&found);
                SendMessageW(gEditor, EM_SCROLLCARET, 0, 0);
                if (gPreviewVisible) {
                    gPreviewVisible = false;
                    ShowWindow(gPreview, SW_HIDE);
                    ShowWindow(gEditor, SW_SHOW);
                }
                SetFocus(gEditor);
            }
            return 0;
        }
        case IDM_HISTORY_RESTORE:
            RestoreEarlierRevision();
            return 0;
        case IDM_EXPORT_DOCX:
        case IDM_EXPORT_HWPX:
        case IDM_EXPORT_PDF:
            ExportFromEditor(id);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (gPreviewVisible) {
                gPreviewVisible = false;
                ShowWindow(gPreview, SW_HIDE);
                ShowWindow(gEditor, SW_SHOW);
            }
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
        0, CLASS_NAME, L"repo portable 0.2.0",
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
        {FVIRTKEY | FCONTROL | FSHIFT, 'O', IDM_CREATE_OBJECT},
        {FVIRTKEY | FCONTROL | FSHIFT, 'P', IDM_PREVIEW},
        {FVIRTKEY | FCONTROL | FSHIFT, 'H', IDM_HISTORY_RESTORE},
        {FVIRTKEY | FCONTROL, 'G', IDM_NEXT_MENTION},
        {FVIRTKEY | FCONTROL, 'T', IDM_TITLE},
        {FVIRTKEY, VK_F1, IDM_HELP}
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
