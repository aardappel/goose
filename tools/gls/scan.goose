// Lossless lexical scan shared by the formatter and language server.
// Tokens keep their source byte offsets. Comments and literals are opaque.
struct SourceToken {
    start: i64, end: i64, kind: i64
}
const NAME_TOKEN = 1;
const NUMBER_TOKEN = 2;
const LITERAL_TOKEN = 3;
const COMMENT_TOKEN = 4;
const PUNCT_TOKEN = 5;

fn source_space(c: u8) -> bool { c == ' ' || c == '\t' || c == '\r' || c == '\n' }
fn source_digit(c: u8) -> bool { c >= '0' && c <= '9' }

fn source_name(c: u8) -> bool {
    (c >= 'a' && c <= 'z') ||(c >= 'A' && c <= 'Z') || c == '_' || c >= 128
}

fn source_hex(c: u8) -> bool { source_digit(c) ||(c >= 'a' && c <= 'f') ||(c >= 'A' && c <= 'F') }

fn source_at(s: const u8[:], i: i64) -> u8 {
    if i < 0 || i >= s.len { return 0; }
    return s[i];
}

fn source_has(s: const u8[:], i: i64, word: const u8[:]) -> bool {
    i >= 0 && i + word.len <= s.len && s[i..i + word.len] == word
}

// Returns an error byte offset, or -1. Incomplete input can still be scanned
// for navigation, but formatting refuses an incomplete literal or delimiter.
fn scan_source(s: const u8[:], out: SourceToken[>..]&) -> i64 {
    var i = 0;
    var brackets: u8[>..] =[];
    while i < s.len {
        if source_space(s[i]) {
            i++;
            continue;
        }
        let start = i;
        var kind = PUNCT_TOKEN;
        if source_has(s, i, "//") {
            kind = COMMENT_TOKEN;
            // Match the compiler lexer: a line comment ends at LF. A bare CR is part of
            // the comment, so formatting cannot expose code after it.
            while i < s.len && s[i] != '\n' { i++; }
            // The CR of a CRLF line break belongs to the break, which the formatter rewrites.
            if i < s.len && s[i - 1] == '\r' { i--; }
        } else if source_has(s, i, "/*") {
            kind = COMMENT_TOKEN;
            i += 2;
            var depth = 1;
            while i < s.len && depth > 0 {
                if source_has(s, i, "/*") {
                    depth++;
                    i += 2;
                }
                else if source_has(s, i, "*/") {
                    depth--;
                    i += 2;
                }
                else { i++; }
            }
            if depth != 0 { return start; }
        } else if source_has(s, i, "\"\"\"") {
            kind = LITERAL_TOKEN;
            i += 3;
            while i < s.len && !source_has(s, i, "\"\"\"") { i++; }
            if i == s.len { return start; }
            i += 3;
        } else if s[i] == '"' || s[i] == '\'' {
            kind = LITERAL_TOKEN;
            let quote = s[i];
            i++;
            while i < s.len && s[i] != quote {
                if s[i] == '\n' || s[i] == '\r' { return start; }
                if s[i] == '\\' { i++; }
                i++;
            }
            if i >= s.len { return start; }
            i++;
        } else if source_name(s[i]) {
            kind = NAME_TOKEN;
            i++;
            while source_name(source_at(s, i)) || source_digit(source_at(s, i)) { i++; }
        } else if source_digit(s[i]) {
            kind = NUMBER_TOKEN;
            let hex = source_has(s, i, "0x") || source_has(s, i, "0X");
            if hex { i += 2; }
            while if hex { source_hex(source_at(s, i)) } else { source_digit(source_at(s, i)) }
            { i++; }
            if source_at(s, i) == '.' &&(if hex { source_hex(source_at(s, i + 1)) } else {
                source_digit(source_at(s, i + 1))
            }) {
                i++;
                while if hex { source_hex(source_at(s, i)) } else { source_digit(source_at(s, i)) }
                { i++; }
            }
            let c = source_at(s, i);
            let exponent = if hex { c == 'p' || c == 'P' } else { c == 'e' || c == 'E' };
            let next = source_at(s, i + 1);
            if exponent &&(source_digit(next) ||((next == '+' || next == '-') && source_digit(source_at(s, i + 2)))) {
                i++;
                if source_at(s, i) == '+' || source_at(s, i) == '-' { i++; }
                while source_digit(source_at(s, i)) { i++; }
            }
        } else {
            var width = 1;
            for op in["<<=", ">>=", ".==", ".!=", "::", "..", ".=", "->", "=>", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "++", "--"] {
                if op.len > width && source_has(s, i, op) { width = op.len; }
            }
            if width == 1 {
                let c = s[i];
                if c == '(' || c == '[' || c == '{' { brackets.push(c); }
                else if c == ')' || c == ']' || c == '}' {
                    if brackets.len == 0 { return i; }
                    let open = brackets.pop();
                    if (c == ')' && open != '(') ||(c == ']' && open != '[') ||(c == '}' && open != '{') {
                        return i;
                    }
                }
                var valid = false;
                for ch in "(){}[];:,.?+-*/%!=<>&|^~" {
                    if c == ch { valid = true; }
                }
                if !valid { return i; }
            }
            i += width;
        }
        out.push(SourceToken { start: start, end: i, kind: kind });
    }
    if brackets.len != 0 { return s.len; }
    return -1;
}

fn token_text(s: const u8[:], t: SourceToken) -> const u8[:] { s[t.start..t.end] }

// Modifiers that precede a declaration keyword, skipping comments between them.
fn next_source_code(ts: SourceToken[:], start: i64) -> i64 {
    var i = start;
    while i < ts.len && ts[i].kind == COMMENT_TOKEN { i++; }
    return i;
}

fn previous_source_code(ts: SourceToken[:], start: i64) -> i64 {
    var i = start;
    while i >= 0 && ts[i].kind == COMMENT_TOKEN { i--; }
    return i;
}

fn declaration_prefix(s: const u8[:], ts: SourceToken[:], keyword: i64) -> i64 {
    var first = keyword;
    loop {
        let p = previous_source_code(ts, first - 1);
        if p < 0 { break; }
        let text = token_text(s, ts[p]);
        if text == "recursive" || text == "export" || text == "extern" || text == "reusable" {
            first = p;
            continue;
        }
        if ts[p].kind == LITERAL_TOKEN {
            let modifier = previous_source_code(ts, p - 1);
            if modifier >= 0 &&(token_text(s, ts[modifier]) == "extern" || token_text(s, ts[modifier]) == "export") {
                first = modifier;
                continue;
            }
        }
        if text == "]" {
            let open = previous_source_code(ts, p - 1);
            let modifier = previous_source_code(ts, open - 1);
            if open >= 0 && modifier >= 0 && token_text(s, ts[open]) == "[" && token_text(s, ts[modifier]) == "reusable" {
                first = modifier;
                continue;
            }
        }
        break;
    }
    return first;
}
