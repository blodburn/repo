//! PDF screenplay document generator.

use repo_core::syntax::{parse_line, ElementKind};

pub fn generate_pdf(manuscript: &str, title: &str) -> Vec<u8> {
    let mut stream_content = String::new();
    stream_content.push_str("BT\n/F1 12 Tf\n16 TL\n");

    let mut y = 780.0;
    stream_content.push_str(&format!("72 {} Td\n", y));

    if !title.is_empty() {
        let escaped_title = title.replace('\\', "\\\\").replace('(', "\\(").replace(')', "\\)");
        stream_content.push_str(&format!("({}) Tj T*\n", escaped_title));
        y -= 24.0;
    }

    for line in manuscript.lines() {
        let parsed = parse_line(line);
        if parsed.kind == ElementKind::Note {
            continue;
        }

        if parsed.kind == ElementKind::Empty {
            stream_content.push_str("T*\n");
            y -= 16.0;
            continue;
        }

        let prefix = match parsed.kind {
            ElementKind::Scene => "[SCENE] ",
            ElementKind::Dialogue => "    ",
            ElementKind::Narration => "  (Narration) ",
            _ => "",
        };

        let formatted = format!("{}{}", prefix, parsed.content);
        let escaped = formatted.replace('\\', "\\\\").replace('(', "\\(").replace(')', "\\)");
        stream_content.push_str(&format!("({}) Tj T*\n", escaped));
        y -= 16.0;

        if y < 72.0 {
            // End page and continue (for single page representation or stream continuation)
            y = 780.0;
        }
    }

    stream_content.push_str("ET\n");

    let stream_bytes = stream_content.as_bytes();
    let stream_len = stream_bytes.len();

    let mut pdf = Vec::new();
    pdf.extend_from_slice(b"%PDF-1.4\n");

    let mut offsets = Vec::new();

    // Obj 1: Catalog
    offsets.push(pdf.len());
    pdf.extend_from_slice(b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    // Obj 2: Pages
    offsets.push(pdf.len());
    pdf.extend_from_slice(b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");

    // Obj 3: Page
    offsets.push(pdf.len());
    pdf.extend_from_slice(b"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595.28 841.89] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>\nendobj\n");

    // Obj 4: Font
    offsets.push(pdf.len());
    pdf.extend_from_slice(b"4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n");

    // Obj 5: Contents
    offsets.push(pdf.len());
    let stream_header = format!("5 0 obj\n<< /Length {} >>\nstream\n", stream_len);
    pdf.extend_from_slice(stream_header.as_bytes());
    pdf.extend_from_slice(stream_bytes);
    pdf.extend_from_slice(b"\nendstream\nendobj\n");

    // Xref
    let xref_offset = pdf.len();
    pdf.extend_from_slice(b"xref\n0 6\n0000000000 65535 f \n");
    for &off in &offsets {
        let entry = format!("{:010} 00000 n \n", off);
        pdf.extend_from_slice(entry.as_bytes());
    }

    // Trailer
    let trailer = format!(
        "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n{}\n%%EOF\n",
        xref_offset
    );
    pdf.extend_from_slice(trailer.as_bytes());

    pdf
}
