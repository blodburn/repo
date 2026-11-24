import Foundation

/// Object discovery for the AppKit UI.
///
/// The grammar and the Korean particle aware mention rules live in the Rust core
/// (`core/src/syntax.rs`, `core/src/objects.rs`); this type only adapts the core's
/// results to the shapes the window controller and the project database expect.
enum ScriptParser {
    static func explicitObjects(in text: String) -> [(name: String, type: String)] {
        RepoCore.explicitObjects(in: text).map { (name: $0.name, type: $0.type) }
    }

    /// Mentions as `(object id, UTF-16 offset)` pairs, ready for the SQLite
    /// `mentions` table and for `NSRange` based navigation.
    static func mentions(in text: String, lexicon: [String: Int64]) -> [(Int64, Int)] {
        guard !lexicon.isEmpty, let engine = RepoCoreEngine() else { return [] }

        for (name, id) in lexicon where !name.isEmpty {
            engine.register(id: String(id), name: name, type: "character")
        }

        return engine.mentions(in: text).compactMap { mention in
            guard let id = Int64(mention.objectID) else { return nil }
            return (id, mention.utf16Offset)
        }
    }
}
