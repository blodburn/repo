import Foundation
import RepoCoreC

/// Element kinds reported by the Rust core (`RepoElementKind` in repo_core.h).
enum RepoCoreElement: Int32 {
    case empty = 0
    case scene = 1
    case dialogue = 2
    case description = 3
    case narration = 4
    case note = 5
    case plainText = 6
}

/// One resolved object mention, positioned the way AppKit addresses the text
/// (`utf16Offset` is an index into an `NSString`/`NSRange`).
struct RepoCoreMention {
    let objectID: String
    let lineIndex: Int
    let utf16Offset: Int
    let byteOffset: Int
    let matchedName: String
}

/// Thin Swift wrapper over the `repo-core` C ABI.
///
/// All screenplay grammar, object registration and Korean particle aware mention
/// scanning live in the Rust engine (`core/`); the AppKit layer only draws.
enum RepoCore {
    /// Semantic version of the linked core, e.g. "0.4.2".
    static var version: String {
        guard let pointer = repo_core_version() else { return "unknown" }
        return String(cString: pointer)
    }

    /// Integer ABI revision of the header the app was compiled against.
    static var abiVersion: UInt32 { repo_core_abi_version() }

    static func classify(_ line: String) -> RepoCoreElement? {
        RepoCoreElement(rawValue: repo_classify_line(line))
    }

    /// Classification and cleaned content (markers stripped, `[[object]]` reduced
    /// to its display name) in a single call.
    static func parse(_ line: String) -> (kind: RepoCoreElement?, content: String) {
        // Cleaned content is never longer than the source line, so one buffer of
        // the source length is always enough.
        var buffer = [CChar](repeating: 0, count: max(1, line.utf8.count + 1))
        let kind = buffer.withUnsafeMutableBufferPointer { pointer in
            repo_parse_line(line, pointer.baseAddress, pointer.count)
        }
        return (RepoCoreElement(rawValue: kind), String(cString: buffer))
    }

    /// `[[김철수]]` -> `김철수`, `[[장소:서울역]]` -> `서울역`.
    static func cleanMarkup(_ text: String) -> String {
        var buffer = [CChar](repeating: 0, count: max(1, text.utf8.count + 1))
        let written = buffer.withUnsafeMutableBufferPointer { pointer in
            repo_clean_markup(text, pointer.baseAddress, pointer.count)
        }
        guard written >= 0 else { return text }
        return String(cString: buffer)
    }

    /// Every explicit `[[...]]` marker in the manuscript, with a normalized type.
    static func explicitObjects(in text: String) -> [(name: String, type: String, alias: String?)] {
        guard let output = fillBuffer({ buffer, length in
            repo_extract_objects(text, buffer, length)
        }) else { return [] }

        return output
            .split(separator: "\n", omittingEmptySubsequences: true)
            .compactMap { record in
                let fields = record.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
                guard fields.count >= 3, !fields[0].isEmpty else { return nil }
                return (fields[0], fields[1], fields[2].isEmpty ? nil : fields[2])
            }
    }

    /// Runs a "returns required size" FFI call with the documented two step
    /// buffer protocol.
    static func fillBuffer(_ body: (UnsafeMutablePointer<CChar>?, Int) -> Int32) -> String? {
        var capacity = 4096
        for _ in 0 ..< 8 {
            var buffer = [CChar](repeating: 0, count: capacity)
            let required = buffer.withUnsafeMutableBufferPointer { pointer in
                body(pointer.baseAddress, pointer.count)
            }
            if required < 0 { return nil }
            if Int(required) <= capacity { return String(cString: buffer) }
            capacity = Int(required) + 1
        }
        return nil
    }

    /// Self test used by `repo-swift --core-self-test` (and by the release
    /// workflow) to prove the Rust engine is linked and behaving.
    static func selfTest() -> [String] {
        var failures: [String] = []

        if version != "0.4.2" { failures.append("core version is \(version), expected 0.4.2") }
        if abiVersion != 1 { failures.append("core ABI is \(abiVersion), expected 1") }

        let kinds: [(String, RepoCoreElement)] = [
            ("", .empty),
            ("# INT. 병원 복도 - 밤", .scene),
            ("@@ 박영희는 어디 있어? @@", .dialogue),
            ("## 형광등이 깜빡인다 ##", .description),
            ("₩₩ 바람이 분다 ₩₩", .narration),
            ("// 감정선 재검토", .note),
            ("그냥 문장", .plainText)
        ]
        for (line, expected) in kinds where classify(line) != expected {
            failures.append("classify(\(line)) was not \(expected)")
        }

        let parsed = parse("## [[김철수]]가 문을 연다 ##")
        if parsed.kind != .description || parsed.content != "김철수가 문을 연다" {
            failures.append("parse() returned \(String(describing: parsed.kind)) / \(parsed.content)")
        }

        if cleanMarkup("[[장소:서울역]]에서 [[철수|김철수]]를 만난다") != "서울역에서 김철수를 만난다" {
            failures.append("cleanMarkup() did not reduce object markup")
        }

        let objects = explicitObjects(in: "[[김철수]]와 [[장소:서울역]]")
        if objects.count != 2 || objects[0].name != "김철수" || objects[0].type != "character"
            || objects[1].name != "서울역" || objects[1].type != "location" {
            failures.append("explicitObjects() returned \(objects)")
        }

        guard let engine = RepoCoreEngine() else {
            failures.append("repo_engine_new() returned NULL")
            return failures
        }
        if !engine.register(id: "17", name: "김철수", type: "character") {
            failures.append("register(id:) failed for a fresh engine")
        }
        if !engine.register(id: "42", name: "서울역", type: "location") {
            failures.append("register(id:) failed for a fresh engine")
        }
        if engine.count != 2 { failures.append("engine count is \(engine.count), expected 2") }

        let mentions = engine.mentions(in: "[[김철수]]가 서울역에서 기다린다.\n김철수산물과는 무관하다.")
        if mentions.count != 2 {
            failures.append("mentions() returned \(mentions.count) records, expected 2 (particle boundary)")
        } else {
            if mentions[0].objectID != "17" || mentions[0].utf16Offset != 2 {
                failures.append("first mention was \(mentions[0].objectID) @ \(mentions[0].utf16Offset)")
            }
            if mentions[1].objectID != "42" { failures.append("second mention was \(mentions[1].objectID)") }
        }
        return failures
    }
}

/// Object index for one parse pass.
///
/// The identifiers handed to `register(id:)` are the project database row ids, so
/// mentions come back already keyed for SQLite.
final class RepoCoreEngine {
    private var handle: OpaquePointer?

    init?() {
        guard let created = repo_engine_new() else { return nil }
        handle = created
    }

    deinit {
        repo_engine_free(handle)
    }

    var count: Int {
        Int(repo_engine_count(handle))
    }

    @discardableResult
    func register(id: String, name: String, type: String) -> Bool {
        guard let handle else { return false }
        return repo_engine_register_with_id(handle, id, name, type) == 1
    }

    func mentions(in text: String) -> [RepoCoreMention] {
        guard let handle else { return [] }
        guard let output = RepoCore.fillBuffer({ buffer, length in
            repo_find_mentions(handle, text, buffer, length)
        }) else { return [] }

        return output
            .split(separator: "\n", omittingEmptySubsequences: true)
            .compactMap { record in
                let fields = record.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
                guard fields.count >= 5,
                      let lineIndex = Int(fields[1]),
                      let utf16 = Int(fields[2]),
                      let byte = Int(fields[3]) else { return nil }
                return RepoCoreMention(objectID: fields[0],
                                       lineIndex: lineIndex,
                                       utf16Offset: utf16,
                                       byteOffset: byte,
                                       matchedName: fields[4])
            }
    }
}
