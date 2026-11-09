//! Hancom Office (.hwpx) screenplay exporter.

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

pub fn generate_hwpx(manuscript: &str, title: &str) -> Vec<u8> {
    let mut zip = ZipArchive::new();

    // 1. mimetype (must be first and uncompressed)
    zip.add_str("mimetype", "application/hwp+zip");

    // 2. version.xml
    zip.add_str("version.xml", r#"<?xml version="1.0" encoding="UTF-8"?>
<ha:HCFVersion xmlns:ha="http://www.hancom.co.kr/hwpml/2011/app"
targetApplication="WORDPROCESSOR" major="5" minor="1" micro="0" buildNumber="0"
osName="Windows" osVersion="10.0"/>"#);

    // 3. META-INF/container.xml
    zip.add_str("META-INF/container.xml", r#"<?xml version="1.0" encoding="UTF-8"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
  <rootfiles>
    <rootfile full-path="Contents/content.hpf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>"#);

    // 4. Contents/content.hpf
    let hpf = format!(r#"<?xml version="1.0" encoding="UTF-8"?>
<opf:package xmlns:opf="http://www.idpf.org/2007/opf/" version="1.4" unique-identifier="uuid">
  <opf:metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>{}</dc:title>
    <dc:language>ko</dc:language>
  </opf:metadata>
  <opf:manifest>
    <opf:item id="header" href="header.xml" media-type="application/xml"/>
    <opf:item id="section0" href="section0.xml" media-type="application/xml"/>
  </opf:manifest>
  <opf:spine>
    <opf:itemref idref="section0"/>
  </opf:spine>
</opf:package>"#, xml_escape(title));
    zip.add_str("Contents/content.hpf", &hpf);

    // 5. Contents/header.xml
    zip.add_str("Contents/header.xml", r#"<?xml version="1.0" encoding="UTF-8"?>
<hh:head xmlns:hh="http://www.hancom.co.kr/hwpml/2011/head" xmlns:hc="http://www.hancom.co.kr/hwpml/2011/core" version="1.4" secCnt="1">
  <hh:beginNum page="1" footnote="1" endnote="1" pic="1" tbl="1" equation="1"/>
  <hh:refList>
    <hh:fontfaces itemCnt="2">
      <hh:fontface lang="HANGUL" fontCnt="1"><hh:font id="0" face="Malgun Gothic" type="TTF" isEmbedded="0"/></hh:fontface>
      <hh:fontface lang="LATIN" fontCnt="1"><hh:font id="0" face="Courier New" type="TTF" isEmbedded="0"/></hh:fontface>
    </hh:fontfaces>
    <hh:charProperties itemCnt="1">
      <hh:charPr id="0" height="1200" textColor="#000000" shadeColor="none" useFontSpace="0" useKerning="0" symMark="NONE" borderFillIDRef="0">
        <hh:fontRef hangul="0" latin="0"/>
        <hh:ratio hangul="100" latin="100"/><hh:spacing hangul="0" latin="0"/><hh:relSz hangul="100" latin="100"/><hh:offset hangul="0" latin="0"/>
      </hh:charPr>
    </hh:charProperties>
    <hh:paraProperties itemCnt="1">
      <hh:paraPr id="0" align="LEFT" tabPrIDRef="0" condense="0" fontLineHeight="0" snapToGrid="0">
        <hh:margin><hc:intent value="0" unit="HWPUNIT"/><hc:left value="0" unit="HWPUNIT"/><hc:right value="0" unit="HWPUNIT"/><hc:prev value="0" unit="HWPUNIT"/><hc:next value="0" unit="HWPUNIT"/></hh:margin>
        <hh:lineSpacing type="PERCENT" value="160" unit="HWPUNIT"/>
      </hh:paraPr>
    </hh:paraProperties>
    <hh:styles itemCnt="1">
      <hh:style id="0" type="PARA" name="바탕글" engName="Normal" paraPrIDRef="0" charPrIDRef="0" nextStyleIDRef="0" langID="1042"/>
    </hh:styles>
  </hh:refList>
</hh:head>"#);

    // 6. Contents/section0.xml
    let mut sec = String::from(r#"<?xml version="1.0" encoding="UTF-8"?>
<hs:sec xmlns:hs="http://www.hancom.co.kr/hwpml/2011/section" xmlns:hp="http://www.hancom.co.kr/hwpml/2011/paragraph">
"#);

    let mut para_id = 1;
    if !title.is_empty() {
        sec.push_str(&format!(
            r#"<hp:p id="{}" paraPrIDRef="0" styleIDRef="0" pageBreak="0" columnBreak="0" merged="0"><hp:run charPrIDRef="0"><hp:t>{}</hp:t></hp:run></hp:p>"#,
            para_id, xml_escape(title)
        ));
        para_id += 1;
    }

    for line in manuscript.lines() {
        let parsed = parse_line(line);
        if parsed.kind == ElementKind::Note {
            continue;
        }

        sec.push_str(&format!(
            r#"<hp:p id="{}" paraPrIDRef="0" styleIDRef="0" pageBreak="0" columnBreak="0" merged="0"><hp:run charPrIDRef="0"><hp:t>{}</hp:t></hp:run></hp:p>"#,
            para_id, xml_escape(&parsed.content)
        ));
        para_id += 1;
    }

    sec.push_str("</hs:sec>");
    zip.add_str("Contents/section0.xml", &sec);

    zip.finish()
}
