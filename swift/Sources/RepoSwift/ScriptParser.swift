import Foundation

enum ScriptParser {
    private static let explicitPattern = #"[[(?:(?<type>[^:]|]+):)?(?<name>[^]|]+)(?:|[^]]+)?]]"#

    static func explicitObjects(in text: String) -> [(name: String, type: String)] {
        guard let regex = try? NSRegularExpression(pattern: explicitPattern) else { return [] }
        let ns = text as NSString
        let range = NSRange(location: 0, length: ns.length)

        return regex.matches(in: text, range: range).compactMap { match in
            let nameRange = match.range(withName: "name")
            guard nameRange.location != NSNotFound else { return nil }
            let name = ns.substring(with: nameRange).trimmingCharacters(in: .whitespacesAndNewlines)
            let typeRange = match.range(withName: "type")
            let rawType = typeRange.location == NSNotFound ? "" : ns.substring(with: typeRange)
            return (name, normalizeType(rawType))
        }
    }

    static func mentions(in text: String, lexicon: [String: Int64]) -> [(Int64, Int)] {
        let ns = text as NSString
        var result: [(Int64, Int)] = []

        for (name, id) in lexicon where !name.isEmpty {
            var searchRange = NSRange(location: 0, length: ns.length)
            while searchRange.length > 0 {
                let range = ns.range(of: name, options: [], range: searchRange)
                if range.location == NSNotFound { break }
                result.append((id, range.location))
                let next = range.location + max(1, range.length)
                if next >= ns.length { break }
                searchRange = NSRange(location: next, length: ns.length - next)
            }
        }
        return result
    }

    private static func normalizeType(_ raw: String) -> String {
        switch raw.trimmingCharacters(in: .whitespacesAndNewlines).lowercased() {
        case "", "인물", "character": return "character"
        case "장소", "location": return "location"
        case "사건", "event": return "event"
        case "조직", "organization": return "organization"
        case "물건", "item": return "item"
        default: return raw
        }
    }
}
