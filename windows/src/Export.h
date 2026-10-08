#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace RepoExport {
enum class Format { WordDocx, HancomHwpx, Pdf };

struct Paragraph {
    enum class Kind { Title, Scene, Character, Dialogue, Description, Narration, Action, Blank };
    Kind kind = Kind::Action;
    std::wstring text;
};

std::vector<Paragraph> Parse(const std::wstring& source, const std::wstring& title,
                             const std::vector<std::wstring>& characterNames);
bool Write(const std::filesystem::path& filename, Format format,
           const std::wstring& source, const std::wstring& title,
           const std::vector<std::wstring>& characterNames, std::wstring& error);
}
