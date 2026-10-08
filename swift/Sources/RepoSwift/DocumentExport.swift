import AppKit
import CoreText
import Foundation

enum ScriptFormat: String {
    case heading, action, character, dialogue, description, narration, blank
}

struct ScriptParagraph {
    let kind: ScriptFormat
    let text: String
}

enum ScriptExportError: Error {
    case cannotWrite
    case cannotCreatePDF
}

enum ScriptDocument {
    static func cleanLinks(_ input: String) -> String {
        let pattern = #"\[\[([^\]]+)\]\]"#
        guard let regex = try? NSRegularExpression(pattern: pattern) else { return input }
        let source = input as NSString
        var output = input
        for match in regex.matches(in: input, range: NSRange(location: 0, length: source.length)).reversed() {
            let raw = source.substring(with: match.range(at: 1))
            let name = raw.split(separator: "|", maxSplits: 1).last.map(String.init) ?? raw
            let display = name.split(separator: ":", maxSplits: 1).last.map(String.init) ?? name
            if let range = Range(match.range, in: output) {
                output.replaceSubrange(range, with: display)
            }
        }
        return output
    }

    static func paragraphs(_ text: String, title: String, names: Set<String>) -> [ScriptParagraph] {
        var output: [ScriptParagraph] = []
        if !title.isEmpty { output.append(ScriptParagraph(kind: .heading, text: title)) }
        for rawLine in text.components(separatedBy: .newlines) {
            let line = rawLine.trimmingCharacters(in: .whitespaces)
            if line.hasPrefix("//") { continue }
            if line.isEmpty { output.append(ScriptParagraph(kind: .blank, text: "")); continue }
            var kind: ScriptFormat = .action
            var result = line
            func unwrap(_ marker: String) -> String? {
                guard result.hasPrefix(marker), result.hasSuffix(marker),
                      result.count >= 2 * marker.count else { return nil }
                return String(result.dropFirst(marker.count).dropLast(marker.count))
                    .trimmingCharacters(in: .whitespacesAndNewlines)
            }
            if let s = unwrap("@@") { result = s; kind = .dialogue }
            else if let s = unwrap("##") { result = s; kind = .description }
            else if let s = unwrap("₩₩") { result = s; kind = .narration }
            else if line.hasPrefix("#") {
                result = String(line.dropFirst()).trimmingCharacters(in: .whitespaces)
                kind = .heading
            }
            result = cleanLinks(result)
            if kind == .action && names.contains(result) { kind = .character }
            output.append(ScriptParagraph(kind: kind, text: result))
        }
        return output
    }

    static func formattedText(_ paragraphs: [ScriptParagraph]) -> NSAttributedString {
        let result = NSMutableAttributedString(string: "")
        let font = NSFont(name: "Menlo", size: 12) ?? .monospacedSystemFont(ofSize: 12, weight: .regular)
        let boldFont = NSFont.monospacedSystemFont(ofSize: 12, weight: .semibold)
        for p in paragraphs {
            let style = NSMutableParagraphStyle()
            style.lineSpacing = 5
            style.paragraphSpacing = 7
            switch p.kind {
            case .heading:
                style.alignment = .left
                style.paragraphSpacingBefore = 13
            case .character:
                style.firstLineHeadIndent = 155
                style.headIndent = 155
            case .dialogue:
                style.firstLineHeadIndent = 90
                style.headIndent = 90
                style.tailIndent = -75
            case .narration:
                style.firstLineHeadIndent = 45
                style.headIndent = 45
                style.tailIndent = -45
            default: break
            }
            let weight: NSFont = (p.kind == .heading || p.kind == .character) ? boldFont : font
            let color: NSColor = {
                switch p.kind {
                case .dialogue: return .textColor
                case .description: return .secondaryLabelColor
                case .narration: return .systemPurple
                default: return .textColor
                }
            }()
            result.append(NSAttributedString(string: p.text + "\n",
                attributes: [.font: weight, .foregroundColor: color, .paragraphStyle: style]))
        }
        return result
    }

    private static func xml(_ source: String) -> String {
        source.replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
            .replacingOccurrences(of: "\"", with: "&quot;")
            .replacingOccurrences(of: "'", with: "&apos;")
    }

    private struct Zip {
        struct Entry { let name: String; let size: UInt32; let crc: UInt32; let offset: UInt32 }
        var data = Data()
        var entries: [Entry] = []

        mutating func u16(_ x: UInt16) {
            data.append(UInt8(truncatingIfNeeded: x))
            data.append(UInt8(truncatingIfNeeded: x >> 8))
        }
        mutating func u32(_ x: UInt32) {
            u16(UInt16(truncatingIfNeeded: x))
            u16(UInt16(truncatingIfNeeded: x >> 16))
        }
        static func crc32(_ bytes: Data) -> UInt32 {
            var crc: UInt32 = 0xFFFF_FFFF
            for b in bytes {
                crc ^= UInt32(b)
                for _ in 0..<8 { crc = (crc >> 1) ^ ((crc & 1) == 1 ? 0xEDB8_8320 : 0) }
            }
            return ~crc
        }
        mutating func add(_ name: String, _ content: String) {
            let payload = Data(content.utf8)
            let nameBytes = Data(name.utf8)
            let size = UInt32(payload.count), crc = Self.crc32(payload)
            let entry = Entry(name: name, size: size, crc: crc, offset: UInt32(data.count))
            u32(0x0403_4b50); u16(20); u16(0); u16(0); u16(0); u16(0)
            u32(crc); u32(size); u32(size)
            u16(UInt16(nameBytes.count)); u16(0)
            data.append(nameBytes); data.append(payload)
            entries.append(entry)
        }
        mutating func finish() {
            let centralOffset = UInt32(data.count)
            for entry in entries {
                let name = Data(entry.name.utf8)
                u32(0x0201_4b50); u16(20); u16(20); u16(0); u16(0); u16(0); u16(0)
                u32(entry.crc); u32(entry.size); u32(entry.size)
                u16(UInt16(name.count)); u16(0); u16(0); u16(0); u16(0)
                u32(0); u32(entry.offset)
                data.append(name)
            }
            let length = UInt32(data.count) - centralOffset
            u32(0x0605_4b50); u16(0); u16(0)
            u16(UInt16(entries.count)); u16(UInt16(entries.count))
            u32(length); u32(centralOffset); u16(0)
        }
        mutating func write(_ url: URL) throws {
            finish()
            try data.write(to: url, options: .atomic)
        }
    }

    static func exportDOCX(to url: URL, paragraphs: [ScriptParagraph]) throws {
        var zip = Zip()
        zip.add("[Content_Types].xml", """
        <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
        <Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
        <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
        <Default Extension="xml" ContentType="application/xml"/>
        <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
        </Types>
        """)
        zip.add("_rels/.rels", """
        <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
        <Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
        <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
        </Relationships>
        """)
        var document = """
        <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
        <w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>
        """
        for p in paragraphs {
            let indent: Int = {
                switch p.kind {
                case .character: return 3100
                case .dialogue: return 1800
                case .narration: return 850
                default: return 0
                }
            }()
            let bold = p.kind == .heading || p.kind == .character
            document += "<w:p><w:pPr>"
            if indent > 0 { document += "<w:ind w:left=\"\(indent)\"/>" }
            document += "<w:spacing w:after=\"90\" w:line=\"360\" w:lineRule=\"auto\"/></w:pPr>"
            document += "<w:r><w:rPr><w:rFonts w:ascii=\"Courier New\" w:hAnsi=\"Courier New\" w:eastAsia=\"Malgun Gothic\"/>"
            document += "<w:sz w:val=\"24\"/>"
            if bold { document += "<w:b/>" }
            document += "</w:rPr><w:t xml:space=\"preserve\">\(xml(p.text))</w:t></w:r></w:p>"
        }
        document += """
        <w:sectPr><w:pgSz w:w="11906" w:h="16838"/>
        <w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440"/>
        </w:sectPr></w:body></w:document>
        """
        zip.add("word/document.xml", document)
        try zip.write(url)
    }

    static func exportHWPX(to url: URL, paragraphs: [ScriptParagraph], title: String) throws {
        var zip = Zip()
        zip.add("mimetype", "application/hwp+zip")
        zip.add("version.xml", """
        <?xml version="1.0" encoding="UTF-8"?>
        <ha:HCFVersion xmlns:ha="http://www.hancom.co.kr/hwpml/2011/app"
        targetApplication="WORDPROCESSOR" major="5" minor="1" micro="0" buildNumber="0"
        osName="macOS" osVersion="13.0"/>
        """)
        zip.add("META-INF/container.xml", """
        <?xml version="1.0" encoding="UTF-8"?>
        <container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
        <rootfiles><rootfile full-path="Contents/content.hpf" media-type="application/oebps-package+xml"/></rootfiles>
        </container>
        """)
        zip.add("Contents/content.hpf", """
        <?xml version="1.0" encoding="UTF-8"?>
        <opf:package xmlns:opf="http://www.idpf.org/2007/opf/" version="1.4" unique-identifier="uuid">
        <opf:metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
        <dc:title>\(xml(title))</dc:title><dc:language>ko</dc:language></opf:metadata>
        <opf:manifest>
        <opf:item id="header" href="header.xml" media-type="application/xml"/>
        <opf:item id="section0" href="section0.xml" media-type="application/xml"/>
        </opf:manifest><opf:spine><opf:itemref idref="section0"/></opf:spine></opf:package>
        """)
        zip.add("Contents/header.xml", """
        <?xml version="1.0" encoding="UTF-8"?>
        <hh:head xmlns:hh="http://www.hancom.co.kr/hwpml/2011/head" xmlns:hc="http://www.hancom.co.kr/hwpml/2011/core" version="1.4" secCnt="1">
        <hh:beginNum page="1" footnote="1" endnote="1" pic="1" tbl="1" equation="1"/>
        <hh:refList><hh:fontfaces itemCnt="2">
        <hh:fontface lang="HANGUL" fontCnt="1"><hh:font id="0" face="Apple SD Gothic Neo" type="TTF" isEmbedded="0"/></hh:fontface>
        <hh:fontface lang="LATIN" fontCnt="1"><hh:font id="0" face="Menlo" type="TTF" isEmbedded="0"/></hh:fontface>
        </hh:fontfaces>
        <hh:charProperties itemCnt="1"><hh:charPr id="0" height="1200" textColor="#000000" shadeColor="none" useFontSpace="0" useKerning="0" symMark="NONE" borderFillIDRef="0"><hh:fontRef hangul="0" latin="0"/><hh:ratio hangul="100" latin="100"/><hh:spacing hangul="0" latin="0"/><hh:relSz hangul="100" latin="100"/><hh:offset hangul="0" latin="0"/></hh:charPr></hh:charProperties>
        <hh:paraProperties itemCnt="1"><hh:paraPr id="0" align="LEFT" tabPrIDRef="0" condense="0" fontLineHeight="0" snapToGrid="0"><hh:margin><hc:intent value="0" unit="HWPUNIT"/><hc:left value="0" unit="HWPUNIT"/><hc:right value="0" unit="HWPUNIT"/><hc:prev value="0" unit="HWPUNIT"/><hc:next value="0" unit="HWPUNIT"/></hh:margin><hh:lineSpacing type="PERCENT" value="160" unit="HWPUNIT"/></hh:paraPr></hh:paraProperties>
        <hh:styles itemCnt="1"><hh:style id="0" type="PARA" name="바탕" engName="Normal" paraPrIDRef="0" charPrIDRef="0" nextStyleIDRef="0" langID="1042"/></hh:styles></hh:refList></hh:head>
        """)
        var section = """
        <?xml version="1.0" encoding="UTF-8"?>
        <hs:sec xmlns:hs="http://www.hancom.co.kr/hwpml/2011/section"
        xmlns:hp="http://www.hancom.co.kr/hwpml/2011/paragraph">
        """
        for (index, p) in paragraphs.enumerated() {
            section += "<hp:p id=\"\(index + 1)\" paraPrIDRef=\"0\" styleIDRef=\"0\" pageBreak=\"0\" columnBreak=\"0\" merged=\"0\"><hp:run charPrIDRef=\"0\"><hp:t>\(xml(p.text))</hp:t></hp:run></hp:p>"
        }
        section += "</hs:sec>"
        zip.add("Contents/section0.xml", section)
        try zip.write(url)
    }

    static func exportPDF(to url: URL, paragraphs: [ScriptParagraph]) throws {
        // CoreText draws actual glyphs to PDF rather than an unsearchable page screenshot.
        let page = CGRect(x: 0, y: 0, width: 595.28, height: 841.89)
        guard let consumer = CGDataConsumer(url: url as CFURL),
              let context = CGContext(consumer: consumer, mediaBox: nil, nil) else {
            throw ScriptExportError.cannotCreatePDF
        }
        let fontRegular = CTFontCreateWithName("AppleSDGothicNeo-Regular" as CFString, 12, nil)
        let fontBold = CTFontCreateWithName("AppleSDGothicNeo-SemiBold" as CFString, 12, nil)
        var y = page.height - 85
        context.beginPDFPage([kCGPDFContextMediaBox as String: page] as CFDictionary)

        for p in paragraphs {
            if p.kind == .blank { y -= 15; continue }
            let x: CGFloat
            let columnWidth: CGFloat
            switch p.kind {
            case .character: (x, columnWidth) = (260, 255)
            case .dialogue: (x, columnWidth) = (175, 295)
            case .narration: (x, columnWidth) = (115, 375)
            default: (x, columnWidth) = (72, 451)
            }
            let font = (p.kind == .heading || p.kind == .character) ? fontBold : fontRegular
            let attributed = NSAttributedString(string: p.text,
                attributes: [NSAttributedString.Key(kCTFontAttributeName as String): font])
            let typesetter = CTTypesetterCreateWithAttributedString(attributed)
            let fullLength = (p.text as NSString).length
            var start = 0
            while start < fullLength {
                let length = max(1, CTTypesetterSuggestLineBreak(typesetter, start, Double(columnWidth)))
                let count = min(length, fullLength - start)
                let line = CTTypesetterCreateLine(typesetter, CFRangeMake(start, count))
                if y < 85 {
                    context.endPDFPage()
                    context.beginPDFPage([kCGPDFContextMediaBox as String: page] as CFDictionary)
                    y = page.height - 85
                }
                context.textPosition = CGPoint(x: x, y: y)
                CTLineDraw(line, context)
                y -= 18
                start += count
            }
            y -= (p.kind == .heading) ? 13 : 7
        }
        context.endPDFPage()
        context.closePDF()
    }
}
