#include "RepoCore.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace RepoCore {
namespace {

// Classified with the C core's own record format: tab separated fields, one
// record per line, terminated by a newline.
std::vector<std::vector<std::string>> SplitRecords(const std::string& records) {
    std::vector<std::vector<std::string>> rows;
    size_t pos = 0;
    while (pos < records.size()) {
        size_t end = records.find('\n', pos);
        if (end == std::string::npos) end = records.size();
        std::string record = records.substr(pos, end - pos);
        pos = end + 1;
        if (record.empty()) continue;

        std::vector<std::string> fields;
        size_t start = 0;
        while (true) {
            size_t tab = record.find('\t', start);
            if (tab == std::string::npos) {
                fields.push_back(record.substr(start));
                break;
            }
            fields.push_back(record.substr(start, tab - start));
            start = tab + 1;
        }
        rows.push_back(std::move(fields));
    }
    return rows;
}

} // namespace

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string Version() {
    const char* version = repo_core_version();
    return version ? std::string(version) : std::string();
}

unsigned AbiVersion() {
    return static_cast<unsigned>(repo_core_abi_version());
}

Kind ClassifyLine(const std::wstring& line) {
    const std::string utf8 = Utf8(line);
    return static_cast<Kind>(repo_classify_line(utf8.c_str()));
}

std::pair<Kind, std::wstring> ParseLine(const std::wstring& line) {
    const std::string utf8 = Utf8(line);
    // Cleaned content is never longer than the source line.
    std::vector<char> buffer(utf8.size() + 1, '\0');
    const int32_t kind = repo_parse_line(utf8.c_str(), buffer.data(), buffer.size());
    return {static_cast<Kind>(kind), Wide(std::string(buffer.data()))};
}

std::wstring CleanMarkup(const std::wstring& text) {
    const std::string utf8 = Utf8(text);
    std::vector<char> buffer(utf8.size() + 1, '\0');
    const int32_t written = repo_clean_markup(utf8.c_str(), buffer.data(), buffer.size());
    if (written < 0) return text;
    return Wide(std::string(buffer.data()));
}

std::vector<ExplicitObject> ExtractObjects(const std::wstring& text) {
    const std::string utf8 = Utf8(text);
    std::vector<ExplicitObject> objects;

    // Two step buffer protocol: probe for the size, then fill.
    int32_t required = repo_extract_objects(utf8.c_str(), nullptr, 0);
    if (required <= 0) return objects;

    std::vector<char> buffer(static_cast<size_t>(required), '\0');
    required = repo_extract_objects(utf8.c_str(), buffer.data(), buffer.size());
    if (required < 0) return objects;

    for (const auto& fields : SplitRecords(std::string(buffer.data()))) {
        if (fields.size() < 3 || fields[0].empty()) continue;
        objects.push_back({Wide(fields[0]), Wide(fields[1]), Wide(fields[2])});
    }
    return objects;
}

Engine::Engine() : handle_(repo_engine_new()) {}

Engine::~Engine() {
    if (handle_) repo_engine_free(handle_);
}

bool Engine::RegisterId(const std::string& objectId, const std::wstring& name, const std::wstring& type) {
    if (!handle_ || objectId.empty() || name.empty()) return false;
    const std::string utf8Name = Utf8(name);
    const std::string utf8Type = Utf8(type);
    return repo_engine_register_with_id(handle_, objectId.c_str(), utf8Name.c_str(),
                                        utf8Type.c_str()) == 1;
}

unsigned Engine::Count() const {
    return handle_ ? static_cast<unsigned>(repo_engine_count(handle_)) : 0u;
}

std::vector<Mention> Engine::FindMentions(const std::wstring& text) const {
    std::vector<Mention> mentions;
    if (!handle_) return mentions;

    const std::string utf8 = Utf8(text);
    int32_t required = repo_find_mentions(handle_, utf8.c_str(), nullptr, 0);
    if (required <= 0) return mentions;

    std::vector<char> buffer(static_cast<size_t>(required), '\0');
    required = repo_find_mentions(handle_, utf8.c_str(), buffer.data(), buffer.size());
    if (required < 0) return mentions;

    for (const auto& fields : SplitRecords(std::string(buffer.data()))) {
        if (fields.size() < 5) continue;
        Mention mention;
        mention.objectId = fields[0];
        mention.lineIndex = std::strtoll(fields[1].c_str(), nullptr, 10);
        mention.utf16Offset = std::strtoll(fields[2].c_str(), nullptr, 10);
        mention.byteOffset = std::strtoll(fields[3].c_str(), nullptr, 10);
        mention.matchedName = Wide(fields[4]);
        mentions.push_back(std::move(mention));
    }
    return mentions;
}

bool SelfTest(std::string& report) {
    std::ostringstream log;
    bool ok = true;
    auto expect = [&](bool condition, const char* what) {
        if (!condition) {
            ok = false;
            log << "FAIL: " << what << "\n";
        }
    };

    expect(Version() == "0.4.2", "repo_core_version()");
    expect(AbiVersion() == 1, "repo_core_abi_version()");

    expect(ClassifyLine(L"# INT. 병원 복도 - 밤") == Kind::Scene, "classify scene");
    auto dialogue = ParseLine(L"@@ \uC548\uB155\uD558\uC138\uC694 @@");
    expect(dialogue.first == Kind::Dialogue, "parse dialogue kind");
    expect(dialogue.second == L"\uC548\uB155\uD558\uC138\uC694", "parse dialogue content");
    auto description = ParseLine(L"## [[\uAE40\uCCA0\uC218]]\uAC00 \uBB38\uC744 \uC5F4\uB2E4 ##");
    expect(description.first == Kind::Description, "parse description kind");
    expect(description.second == L"\uAE40\uCCA0\uC218\uAC00 \uBB38\uC744 \uC5F4\uB2E4",
           "parse description cleans markup");
    expect(ClassifyLine(L"// \uBA54\uBAA8") == Kind::Note, "classify note");

    auto objects = ExtractObjects(L"[[\uAE40\uCCA0\uC218]] [[\uC7A5\uC18C:\uC11C\uC6B8\uC5ED]]");
    expect(objects.size() == 2, "extract objects count");
    if (objects.size() == 2) {
        expect(objects[0].type == L"character", "extract objects character");
        expect(objects[1].name == L"\uC11C\uC6B8\uC5ED", "extract objects location name");
        expect(objects[1].type == L"location", "extract objects location type");
    }

    Engine engine;
    expect(engine.valid(), "repo_engine_new()");
    expect(engine.RegisterId("17", L"\uAE40\uCCA0\uC218", L"character"), "register id 17");
    expect(engine.RegisterId("42", L"\uC11C\uC6B8\uC5ED", L"location"), "register id 42");
    expect(engine.Count() == 2, "engine count");

    // "김철수산물" must not be reported thanks to the Korean particle boundary rule.
    const std::wstring manuscript =
        L"[[\uAE40\uCCA0\uC218]]\uAC00 \uC11C\uC6B8\uC5ED\uC5D0\uC11C \uAE30\uB2E4\uB9B0\uB2E4.\n"
        L"\uAE40\uCCA0\uC218\uC0B0\uBB3C\uACFC\uB294 \uBB34\uAD00\uD558\uB2E4.";
    auto mentions = engine.FindMentions(manuscript);
    expect(mentions.size() == 2, "mention count with particle boundary");
    if (mentions.size() == 2) {
        expect(mentions[0].objectId == "17", "mention object id");
        expect(mentions[0].utf16Offset == 2, "mention utf16 offset");
        expect(mentions[1].objectId == "42", "mention location id");
    }

    log << "repo-core " << Version() << " (abi " << AbiVersion() << ") self test "
        << (ok ? "passed" : "failed") << "\n";
    report = log.str();
    return ok;
}

} // namespace RepoCore
