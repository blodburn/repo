#include "Export.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace fs = std::filesystem;
namespace RepoExport {
namespace {

std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), result.data(), n, nullptr, nullptr);
    return result;
}

std::wstring Trim(std::wstring s) {
    auto ws = [](wchar_t c) { return iswspace(c) != 0; };
    while (!s.empty() && ws(s.front())) s.erase(s.begin());
    while (!s.empty() && ws(s.back())) s.pop_back();
    return s;
}

std::wstring CleanObjectMarkup(const std::wstring& input) {
    std::wstring out;
    size_t pos = 0;
    while (pos < input.size()) {
        size_t start = input.find(L"[[", pos);
        if (start == std::wstring::npos) { out += input.substr(pos); break; }
        out += input.substr(pos, start - pos);
        size_t end = input.find(L"]]", start + 2);
        if (end == std::wstring::npos) { out += input.substr(start); break; }
        std::wstring inner = input.substr(start + 2, end - start - 2);
        size_t bar = inner.find(L'|');
        if (bar != std::wstring::npos) inner = inner.substr(bar + 1);
        else {
            size_t colon = inner.find(L':');
            if (colon != std::wstring::npos) inner = inner.substr(colon + 1);
        }
        out += inner;
        pos = end + 2;
    }
    return out;
}

std::string XmlEscape(const std::wstring& text) {
    std::string s = Utf8(text), out;
    out.reserve(s.size() + 12);
    for (char ch : s) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += ch; break;
        }
    }
    return out;
}

void Append16(std::string& buf, uint16_t v) {
    buf += char(v & 255); buf += char((v >> 8) & 255);
}
void Append32(std::string& buf, uint32_t v) {
    Append16(buf, uint16_t(v & 65535)); Append16(buf, uint16_t(v >> 16));
}

class ZipWriter {
    struct Entry { std::string name; uint32_t size, crc, offset; };
    std::string bytes_;
    std::vector<Entry> entries_;
public:
    void add(const std::string& name, const std::string& data) {
        Entry e{name, uint32_t(data.size()), uint32_t(crc32(
            0, reinterpret_cast<const Bytef*>(data.data()), uInt(data.size()))),
            uint32_t(bytes_.size())};
        Append32(bytes_, 0x04034b50);
        Append16(bytes_, 20); Append16(bytes_, 0); Append16(bytes_, 0);
        Append16(bytes_, 0); Append16(bytes_, 0);
        Append32(bytes_, e.crc); Append32(bytes_, e.size); Append32(bytes_, e.size);
        Append16(bytes_, uint16_t(name.size())); Append16(bytes_, 0);
        bytes_ += name; bytes_ += data;
        entries_.push_back(e);
    }
    bool save(const fs::path& filename) {
        uint32_t offset = uint32_t(bytes_.size());
        for (const Entry& e : entries_) {
            Append32(bytes_, 0x02014b50);
            Append16(bytes_, 20); Append16(bytes_, 20);
            Append16(bytes_, 0); Append16(bytes_, 0);
            Append16(bytes_, 0); Append16(bytes_, 0);
            Append32(bytes_, e.crc); Append32(bytes_, e.size); Append32(bytes_, e.size);
            Append16(bytes_, uint16_t(e.name.size())); Append16(bytes_, 0);
            Append16(bytes_, 0); Append16(bytes_, 0); Append16(bytes_, 0);
            Append32(bytes_, 0); Append32(bytes_, e.offset);
            bytes_ += e.name;
        }
        uint32_t directorySize = uint32_t(bytes_.size()) - offset;
        Append32(bytes_, 0x06054b50);
        Append16(bytes_, 0); Append16(bytes_, 0);
        Append16(bytes_, uint16_t(entries_.size()));
        Append16(bytes_, uint16_t(entries_.size()));
        Append32(bytes_, directorySize); Append32(bytes_, offset);
        Append16(bytes_, 0);
        std::ofstream out(filename, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(bytes_.data(), std::streamsize(bytes_.size()));
        return out.good();
    }
};

bool Docx(const fs::path& file, const std::vector<Paragraph>& paragraphs) {
    ZipWriter zip;
    zip.add("[Content_Types].xml",
R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>)");
    zip.add("_rels/.rels",
R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>)");

    std::string xml =
R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body>)";
    for (const auto& p : paragraphs) {
        int left = 0;
        bool bold = false, center = false;
        switch (p.kind) {
        case Paragraph::Kind::Title: center = true; bold = true; break;
        case Paragraph::Kind::Scene: bold = true; break;
        case Paragraph::Kind::Character: left = 3100; bold = true; break;
        case Paragraph::Kind::Dialogue: left = 1800; break;
        case Paragraph::Kind::Narration: left = 850; break;
        default: break;
        }
        xml += "<w:p><w:pPr>";
        if (left) xml += "<w:ind w:left=\"" + std::to_string(left) + "\"/>";
        if (center) xml += "<w:jc w:val=\"center\"/>";
        xml += "<w:spacing w:before=\"60\" w:after=\"80\" w:line=\"360\" w:lineRule=\"auto\"/>";
        xml += "</w:pPr><w:r><w:rPr>";
        xml += "<w:rFonts w:ascii=\"Courier New\" w:hAnsi=\"Courier New\" w:eastAsia=\"Malgun Gothic\"/>";
        xml += "<w:sz w:val=\"24\"/><w:szCs w:val=\"24\"/>";
        if (bold) xml += "<w:b/>";
        xml += "</w:rPr><w:t xml:space=\"preserve\">" + XmlEscape(p.text) + "</w:t></w:r></w:p>";
    }
    xml += R"(<w:sectPr><w:pgSz w:w="11906" w:h="16838"/>
<w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="708" w:footer="708" w:gutter="0"/>
</w:sectPr></w:body></w:document>)";
    zip.add("word/document.xml", xml);
    return zip.save(file);
}

// A deliberately limited plain-text HWPX package: no custom table/image content.
// The section content uses the standard HWPX 2011 paragraph schema.
bool Hwpx(const fs::path& file, const std::vector<Paragraph>& paragraphs, const std::wstring& title) {
    ZipWriter zip;
    zip.add("mimetype", "application/hwp+zip");
    zip.add("version.xml",
        R"(<?xml version="1.0" encoding="UTF-8"?><ha:HCFVersion xmlns:ha="http://www.hancom.co.kr/hwpml/2011/app" targetApplication="WORDPROCESSOR" major="5" minor="1" micro="0" buildNumber="0" osName="Windows" osVersion="10.0"/>)");
    zip.add("META-INF/container.xml",
        R"(<?xml version="1.0" encoding="UTF-8"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="Contents/content.hpf" media-type="application/oebps-package+xml"/></rootfiles></container>)");
    std::string content =
        R"(<?xml version="1.0" encoding="UTF-8"?><opf:package xmlns:opf="http://www.idpf.org/2007/opf/" version="1.4" unique-identifier="uuid"><opf:metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>)"
        + XmlEscape(title) +
        R"(</dc:title><dc:language>ko</dc:language></opf:metadata><opf:manifest><opf:item id="header" href="header.xml" media-type="application/xml"/><opf:item id="section0" href="section0.xml" media-type="application/xml"/></opf:manifest><opf:spine><opf:itemref idref="section0"/></opf:spine></opf:package>)";
    zip.add("Contents/content.hpf", content);
    zip.add("Contents/header.xml",
        R"(<?xml version="1.0" encoding="UTF-8"?>
<hh:head xmlns:hh="http://www.hancom.co.kr/hwpml/2011/head" xmlns:hc="http://www.hancom.co.kr/hwpml/2011/core" version="1.4" secCnt="1">
<hh:beginNum page="1" footnote="1" endnote="1" pic="1" tbl="1" equation="1"/>
<hh:refList>
<hh:fontfaces itemCnt="2">
<hh:fontface lang="HANGUL" fontCnt="1"><hh:font id="0" face="맑은 고딕" type="TTF" isEmbedded="0"/></hh:fontface>
<hh:fontface lang="LATIN" fontCnt="1"><hh:font id="0" face="Courier New" type="TTF" isEmbedded="0"/></hh:fontface>
</hh:fontfaces>
<hh:charProperties itemCnt="1"><hh:charPr id="0" height="1200" textColor="#000000" shadeColor="none" useFontSpace="0" useKerning="0" symMark="NONE" borderFillIDRef="0"><hh:fontRef hangul="0" latin="0"/><hh:ratio hangul="100" latin="100"/><hh:spacing hangul="0" latin="0"/><hh:relSz hangul="100" latin="100"/><hh:offset hangul="0" latin="0"/></hh:charPr></hh:charProperties>
<hh:paraProperties itemCnt="1"><hh:paraPr id="0" align="LEFT" tabPrIDRef="0" condense="0" fontLineHeight="0" snapToGrid="0"><hh:margin><hc:intent value="0" unit="HWPUNIT"/><hc:left value="0" unit="HWPUNIT"/><hc:right value="0" unit="HWPUNIT"/><hc:prev value="0" unit="HWPUNIT"/><hc:next value="0" unit="HWPUNIT"/></hh:margin><hh:lineSpacing type="PERCENT" value="160" unit="HWPUNIT"/></hh:paraPr></hh:paraProperties>
<hh:styles itemCnt="1"><hh:style id="0" type="PARA" name="바탕" engName="Normal" paraPrIDRef="0" charPrIDRef="0" nextStyleIDRef="0" langID="1042"/></hh:styles>
</hh:refList>
</hh:head>)");
    std::string section =
        R"(<?xml version="1.0" encoding="UTF-8"?><hs:sec xmlns:hs="http://www.hancom.co.kr/hwpml/2011/section" xmlns:hp="http://www.hancom.co.kr/hwpml/2011/paragraph">)";
    int id = 1;
    for (const auto& p : paragraphs) {
        section += "<hp:p id=\"" + std::to_string(id++) + "\" paraPrIDRef=\"0\" styleIDRef=\"0\" pageBreak=\"0\" columnBreak=\"0\" merged=\"0\"><hp:run charPrIDRef=\"0\"><hp:t>"
                 + XmlEscape(p.text) + "</hp:t></hp:run></hp:p>";
    }
    section += "</hs:sec>";
    zip.add("Contents/section0.xml", section);
    return zip.save(file);
}

struct PdfWriter {
    std::string out{"%PDF-1.4\n%\xC2\xA5\xC2\xB1\n"};
    std::vector<size_t> offsets{0};
    void obj(int n, const std::string& body) {
        while (offsets.size() <= size_t(n)) offsets.push_back(0);
        offsets[n] = out.size();
        out += std::to_string(n) + " 0 obj\n" + body + "\nendobj\n";
    }
    void stream(int n, const std::string& dict, const std::string& bytes) {
        obj(n, dict + "\nstream\n" + bytes + "\nendstream");
    }
    bool save(const fs::path& filename, int root) {
        size_t xref = out.size();
        out += "xref\n0 " + std::to_string(offsets.size()) + "\n0000000000 65535 f \n";
        for (size_t i = 1; i < offsets.size(); ++i) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%010llu 00000 n \n", (unsigned long long)offsets[i]);
            out += buf;
        }
        out += "trailer\n<< /Size " + std::to_string(offsets.size())
             + " /Root " + std::to_string(root) + " 0 R >>\nstartxref\n"
             + std::to_string(xref) + "\n%%EOF\n";
        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(out.data(), std::streamsize(out.size()));
        return file.good();
    }
};

bool Pdf(const fs::path& file, const std::vector<Paragraph>& paragraphs) {
    constexpr int width = 1240, height = 1754;
    constexpr int margin = 115, top = 120, bottom = 130;
    constexpr double pageWidth = 595.276, pageHeight = 841.89;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !bmp || !pixels) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return false;
    }
    HGDIOBJ old = SelectObject(dc, bmp);
    HFONT font = CreateFontW(-23, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic");
    HFONT bold = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic");
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0,0,0));

    PdfWriter pdf;
    std::vector<std::string> images;
    auto clear = [&]() {
        RECT r{0,0,width,height};
        FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    };
    auto storePage = [&]() {
        std::string bits((width + 7) / 8 * height, '\0');
        auto* px = static_cast<const uint32_t*>(pixels);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                uint32_t c = px[y * width + x];
                unsigned b = (c & 0xff), g = (c >> 8) & 0xff, r = (c >> 16) & 0xff;
                bool white = (r + g + b) > 380;
                if (white) bits[size_t(y) * ((width + 7) / 8) + x / 8] |= char(0x80 >> (x & 7));
            }
        }
        uLongf capacity = compressBound(uLong(bits.size()));
        std::string encoded(capacity, '\0');
        if (compress2(reinterpret_cast<Bytef*>(encoded.data()), &capacity,
                reinterpret_cast<const Bytef*>(bits.data()), uLong(bits.size()), Z_BEST_COMPRESSION) != Z_OK) return;
        encoded.resize(capacity);
        images.push_back(std::move(encoded));
    };
    clear();
    int y = top;
    for (const auto& para : paragraphs) {
        if (para.kind == Paragraph::Kind::Blank) { y += 18; continue; }
        int left = margin;
        int areaWidth = width - margin * 2;
        bool highlight = false;
        switch (para.kind) {
        case Paragraph::Kind::Title: highlight = true; left = margin; break;
        case Paragraph::Kind::Scene: highlight = true; break;
        case Paragraph::Kind::Character: highlight = true; left = 410; areaWidth = 560; break;
        case Paragraph::Kind::Dialogue: left = 300; areaWidth = 630; break;
        case Paragraph::Kind::Narration: left = 235; areaWidth = 810; break;
        default: break;
        }
        SelectObject(dc, highlight ? bold : font);
        RECT measure{left, 0, left + areaWidth, 0};
        std::wstring s = para.text.empty() ? L" " : para.text;
        DrawTextW(dc, s.c_str(), -1, &measure, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        int blockHeight = std::max(34, int(measure.bottom - measure.top) + 11);
        if (y + blockHeight > height - bottom) {
            storePage();
            clear();
            y = top;
        }
        RECT draw{left, y, left + areaWidth, y + blockHeight};
        DrawTextW(dc, s.c_str(), -1, &draw, DT_WORDBREAK | DT_NOPREFIX);
        y += blockHeight + ((para.kind == Paragraph::Kind::Scene) ? 17 : 6);
    }
    storePage();
    SelectObject(dc, old);
    DeleteObject(font); DeleteObject(bold); DeleteObject(bmp); DeleteDC(dc);
    if (images.empty()) return false;

    std::string kids;
    for (size_t i = 0; i < images.size(); ++i) {
        if (!kids.empty()) kids += " ";
        kids += std::to_string(3 + i * 3) + " 0 R";
    }
    pdf.obj(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.obj(2, "<< /Type /Pages /Count " + std::to_string(images.size())
                 + " /Kids [" + kids + "] >>");
    for (size_t i = 0; i < images.size(); ++i) {
        int id = 3 + int(i) * 3;
        int contentsID = id + 1, imageID = id + 2;
        pdf.obj(id, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
               + std::to_string(pageWidth) + " " + std::to_string(pageHeight)
               + "] /Resources << /XObject << /Im0 " + std::to_string(imageID)
               + " 0 R >> >> /Contents " + std::to_string(contentsID) + " 0 R >>");
        std::string cmd = "q " + std::to_string(pageWidth) + " 0 0 "
                        + std::to_string(pageHeight) + " 0 0 cm /Im0 Do Q\n";
        pdf.stream(contentsID, "<< /Length " + std::to_string(cmd.size()) + " >>", cmd);
        const std::string& image = images[i];
        pdf.stream(imageID, "<< /Type /XObject /Subtype /Image /Width "
            + std::to_string(width) + " /Height " + std::to_string(height)
            + " /ColorSpace /DeviceGray /BitsPerComponent 1 /Filter /FlateDecode /Length "
            + std::to_string(image.size()) + " >>", image);
    }
    return pdf.save(file, 1);
}
} // namespace

std::vector<Paragraph> Parse(const std::wstring& source, const std::wstring& title,
                             const std::vector<std::wstring>& characterNames) {
    std::vector<Paragraph> out;
    if (!title.empty()) out.push_back({Paragraph::Kind::Title, title});
    std::set<std::wstring> names(characterNames.begin(), characterNames.end());

    size_t pos = 0;
    while (pos <= source.size()) {
        size_t end = source.find(L'\n', pos);
        if (end == std::wstring::npos) end = source.size();
        std::wstring line = Trim(source.substr(pos, end - pos));
        pos = end + 1;
        if (line.size() >= 2 && line.substr(0,2) == L"//") {
            if (end == source.size()) break;
            continue;
        }
        Paragraph::Kind kind = Paragraph::Kind::Action;
        if (line.empty()) kind = Paragraph::Kind::Blank;
        else if (line.size() >= 4 && line.substr(0,2) == L"@@" && line.substr(line.size()-2) == L"@@") {
            line = Trim(line.substr(2, line.size()-4)); kind = Paragraph::Kind::Dialogue;
        } else if (line.size() >= 4 && line.substr(0,2) == L"##" && line.substr(line.size()-2) == L"##") {
            line = Trim(line.substr(2, line.size()-4)); kind = Paragraph::Kind::Description;
        } else if (line.size() >= 4 && line.substr(0,2) == L"₩₩" && line.substr(line.size()-2) == L"₩₩") {
            line = Trim(line.substr(2, line.size()-4)); kind = Paragraph::Kind::Narration;
        } else if (!line.empty() && line[0] == L'#') {
            line = Trim(line.substr(1)); kind = Paragraph::Kind::Scene;
        }
        line = CleanObjectMarkup(line);
        if (kind == Paragraph::Kind::Action && names.count(line)) kind = Paragraph::Kind::Character;
        out.push_back({kind, line});
        if (end == source.size()) break;
    }
    return out;
}

bool Write(const fs::path& filename, Format format, const std::wstring& source,
           const std::wstring& title, const std::vector<std::wstring>& names, std::wstring& error) {
    auto paragraphs = Parse(source, title, names);
    bool ok = false;
    switch (format) {
    case Format::WordDocx: ok = Docx(filename, paragraphs); break;
    case Format::HancomHwpx: ok = Hwpx(filename, paragraphs, title); break;
    case Format::Pdf: ok = Pdf(filename, paragraphs); break;
    }
    if (!ok) error = L"문서를 생성하지 못했습니다. 저장 경로와 권한을 확인하세요.";
    return ok;
}
} // namespace RepoExport
