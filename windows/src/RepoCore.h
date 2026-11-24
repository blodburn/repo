#pragma once
// C++ wrapper over repo-core (core/include/repo_core.h).
//
// Both Win32 sources (main.cpp, Export.cpp) go through this header so the
// screenplay grammar, object index and mention scanning come from the same Rust
// engine the macOS app uses. Strings cross the C ABI as UTF-8; these helpers
// convert to and from the UTF-16 std::wstring the Win32 UI works in.

#include <string>
#include <utility>
#include <vector>

#include "repo_core.h"

namespace RepoCore {

enum class Kind {
    Empty = REPO_ELEM_EMPTY,
    Scene = REPO_ELEM_SCENE,
    Dialogue = REPO_ELEM_DIALOGUE,
    Description = REPO_ELEM_DESCRIPTION,
    Narration = REPO_ELEM_NARRATION,
    Note = REPO_ELEM_NOTE,
    PlainText = REPO_ELEM_PLAIN_TEXT,
    Error = REPO_ELEM_ERROR
};

struct ExplicitObject {
    std::wstring name;
    std::wstring type;
    std::wstring alias;
};

struct Mention {
    std::string objectId;      // identifier supplied to Engine::RegisterId
    long long lineIndex = 0;
    long long utf16Offset = 0; // index into std::wstring / RICHEDIT character position
    long long byteOffset = 0;  // index into the UTF-8 manuscript
    std::wstring matchedName;
};

std::string Utf8(const std::wstring& text);
std::wstring Wide(const std::string& text);

// Semantic version of the linked core.
std::string Version();
unsigned AbiVersion();

// One line: classification plus cleaned content (markers stripped).
std::pair<Kind, std::wstring> ParseLine(const std::wstring& line);
Kind ClassifyLine(const std::wstring& line);
std::wstring CleanMarkup(const std::wstring& text);

// Every explicit [[...]] marker in the manuscript, with normalized types.
std::vector<ExplicitObject> ExtractObjects(const std::wstring& text);

// Object index for one parse pass. Identifiers are the project database ids, so
// mentions come back keyed for the mentions table.
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool valid() const { return handle_ != nullptr; }
    bool RegisterId(const std::string& objectId, const std::wstring& name, const std::wstring& type);
    unsigned Count() const;
    std::vector<Mention> FindMentions(const std::wstring& text) const;

private:
    RepoEngine* handle_ = nullptr;
};

// Exercises the C ABI end to end: version, classification, markup cleaning,
// object extraction and mention scanning. Used by `repo.exe --core-self-test`.
bool SelfTest(std::string& report);

} // namespace RepoCore
