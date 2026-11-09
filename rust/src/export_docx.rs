//! Microsoft Word (.docx) screenplay exporter.

use crate::zip_writer::ZipArchive;
use repo_core::syntax::{parse_line, ElementKind};

fn xml_escape(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    for ch in text.chars() {
        match ch {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            '\'' => out.push_str("&apos;"),
            _ => out.push(ch),
        }
    }
    out
}

pub fn generate_docx(manuscript: &str, title: &str) -> Vec<u8> {
    let mut zip = ZipArchive::new();

    // 1. [Content_Types].xml
    zip.add_str("[Content_Types].xml", r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>"#);

    // 2. _rels/.rels
    zip.add_str("_rels/.rels", r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>"#);

    // 3. word/document.xml
    let mut doc = String::from(r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body>
"#);

    if !title.is_empty() {
        doc.push_str(&format!(
            r#"<w:p><w:pPr><w:spacing w:after="240" w:line="360" w:lineRule="auto"/></w:pPr><w:r><w:rPr><w:rFonts w:ascii="Courier New" w:hAnsi="Courier New" w:eastAsia="Malgun Gothic"/><w:sz w:val="32"/><w:b/></w:rPr><w:t xml:space="preserve">{}</w:t></w:r></w:p>"#,
            xml_escape(title)
        ));
    }

    for line in manuscript.lines() {
        let parsed = parse_line(line);
        if parsed.kind == ElementKind::Note {
            continue;
        }

        let (indent, bold) = match parsed.kind {
            ElementKind::Scene => (0, true),
            ElementKind::Dialogue => (1800, false),
            ElementKind::Description => (0, false),
            ElementKind::Narration => (850, false),
            ElementKind::PlainText => (0, false),
            ElementKind::Empty | ElementKind::Note => (0, false),
        };

        let mut p_xml = String::from("<w:p><w:pPr>");
        if indent > 0 {
            p_xml.push_str(&format!(r#"<w:ind w:left="{}"/>"#, indent));
        }
        p_xml.push_str(r#"<w:spacing w:after="90" w:line="360" w:lineRule="auto"/></w:pPr>"#);

        if !parsed.content.is_empty() {
            p_xml.push_str(r#"<w:r><w:rPr><w:rFonts w:ascii="Courier New" w:hAnsi="Courier New" w:eastAsia="Malgun Gothic"/><w:sz w:val="24"/>"#);
            if bold {
                p_xml.push_str("<w:b/>");
            }
            p_xml.push_str(&format!(r#"</w:rPr><w:t xml:space="preserve">{}</w:t></w:r>"#, xml_escape(&parsed.content)));
        }
        p_xml.push_str("</w:p>\n");
        doc.push_str(&p_xml);
    }

    doc.push_str(r#"<w:sectPr><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440"/></w:sectPr></w:body></w:document>"#);
    zip.add_str("word/document.xml", &doc);

    zip.finish()
}
