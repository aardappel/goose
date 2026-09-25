// Token kinds: end, identifier/keyword, integer, float, string, punctuation.
const TK_END = 0;
const TK_NAME = 1;
const TK_INT = 2;
const TK_FLOAT = 3;
const TK_STRING = 4;
const TK_PUNCT = 5;

fn digit(ctx: Compiler&, c: u8) -> bool { c >= '0' && c <= '9' }
fn alpha(ctx: Compiler&, c: u8) -> bool { (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' }
fn hex_digit(ctx: Compiler&, c: u8) -> i64 {
    if digit(ctx, c) { return c - '0'; }
    if c >= 'a' && c <= 'f' { return c - 'a' + 10; }
    if c >= 'A' && c <= 'F' { return c - 'A' + 10; }
    return -1;
}

fn lex(ctx: Compiler&, file: i64) {
    let input = ctx.sources[file].text;
    var pos = 0;
    var line = 1;
    var column = 1;
    ctx.sources[file].first = ctx.tokens.len;

    fn peek(ctx: Compiler&, ahead: i64) -> u8 {
        if pos + ahead >= input.len { return 0; }
        return input[pos + ahead];
    }
    fn advance(ctx: Compiler&) -> u8 {
        let c = input[pos];
        pos++;
        if c == '\n' { line++; column = 1; }
        else { column++; }
        return c;
    }
    fn fail(ctx: Compiler&, message: const u8[:]) { diagnostic(ctx, file, line, column, message); }
    fn escape(ctx: Compiler&) -> u8 {
        if pos >= input.len { fail(ctx, "unfinished escape"); }
        let c = advance(ctx);
        if c == 'n' { return '\n'; }
        if c == 'r' { return '\r'; }
        if c == 't' { return '\t'; }
        if c == '0' { return 0; }
        if c == '\\' || c == '\'' || c == '"' { return c; }
        if c == 'x' {
            let hi = hex_digit(ctx, peek(ctx, 0));
            let lo = hex_digit(ctx, peek(ctx, 1));
            if hi < 0 || lo < 0 { fail(ctx, "hex escape requires two hex digits"); }
            advance(ctx); advance(ctx);
            return (hi * 16 + lo) as u8;
        }
        fail(ctx, "unknown escape");
        return 0;
    }

    while pos < input.len {
        let c = peek(ctx, 0);
        if c == ' ' || c == '\t' || c == '\r' || c == '\n' { advance(ctx); continue; }
        if c == '/' && peek(ctx, 1) == '/' {
            while pos < input.len && peek(ctx, 0) != '\n' { advance(ctx); }
            continue;
        }
        if c == '/' && peek(ctx, 1) == '*' {
            advance(ctx); advance(ctx);
            var depth = 1;
            while depth > 0 {
                if pos >= input.len { fail(ctx, "unterminated block comment"); }
                if peek(ctx, 0) == '/' && peek(ctx, 1) == '*' { advance(ctx); advance(ctx); depth++; }
                else if peek(ctx, 0) == '*' && peek(ctx, 1) == '/' { advance(ctx); advance(ctx); depth--; }
                else { advance(ctx); }
            }
            continue;
        }
        let start = pos;
        let start_line = line;
        let start_column = column;
        var kind = TK_PUNCT;
        var value: u64 = 0;
        var decoded = -1;
        var decoded_end = -1;
        if alpha(ctx, c) {
            advance(ctx);
            while alpha(ctx, peek(ctx, 0)) || digit(ctx, peek(ctx, 0)) { advance(ctx); }
            kind = TK_NAME;
        } else if digit(ctx, c) {
            kind = TK_INT;
            var base: u64 = 10;
            if c == '0' && (peek(ctx, 1) == 'x' || peek(ctx, 1) == 'X') { advance(ctx); advance(ctx); base = 16; }
            let digits_start = pos;
            var overflow = false;
            while hex_digit(ctx, peek(ctx, 0)) >= 0 && (hex_digit(ctx, peek(ctx, 0)) as u64) < base {
                let d = hex_digit(ctx, advance(ctx)) as u64;
                if value > (18446744073709551615 - d) / base { overflow = true; }
                value = value * base + d;
            }
            if pos == digits_start { fail(ctx, "expected digits in number"); }
            if peek(ctx, 0) == '.' && hex_digit(ctx, peek(ctx, 1)) >= 0 && (hex_digit(ctx, peek(ctx, 1)) as u64) < base {
                advance(ctx); kind = TK_FLOAT;
                while hex_digit(ctx, peek(ctx, 0)) >= 0 && (hex_digit(ctx, peek(ctx, 0)) as u64) < base {
                    advance(ctx);
                }
            }
            let e = peek(ctx, 0);
            var exponent_seen = false;
            if (base == 10 && (e == 'e' || e == 'E')) || (base == 16 && (e == 'p' || e == 'P')) {
                advance(ctx); kind = TK_FLOAT; exponent_seen = true;
                if peek(ctx, 0) == '+' || peek(ctx, 0) == '-' { advance(ctx); }
                if !digit(ctx, peek(ctx, 0)) { fail(ctx, "expected exponent digits"); }
                while digit(ctx, peek(ctx, 0)) { advance(ctx); }
            }
            if base == 16 && kind == TK_FLOAT && !exponent_seen { fail(ctx, "hex float requires a binary exponent"); }
            if kind == TK_INT && overflow { fail(ctx, "integer literal exceeds u64"); }
            if alpha(ctx, peek(ctx, 0)) { fail(ctx, "invalid number suffix"); }
        } else if c == '"' || c == '\'' {
            advance(ctx);
            decoded = ctx.text_storage.len;
            if c == '"' && peek(ctx, 0) == '"' && peek(ctx, 1) == '"' {
                advance(ctx); advance(ctx);
                let multiline = peek(ctx, 0) == '\n' || peek(ctx, 0) == '\r';
                if multiline {
                    if peek(ctx, 0) == '\r' { advance(ctx); }
                    if peek(ctx, 0) != '\n' { fail(ctx, "expected newline after raw string opener"); }
                    advance(ctx);
                }
                let content = pos;
                var last_line = pos;
                while !(peek(ctx, 0) == '"' && peek(ctx, 1) == '"' && peek(ctx, 2) == '"') {
                    if pos >= input.len { fail(ctx, "unterminated raw string"); }
                    if !multiline && (peek(ctx, 0) == '\n' || peek(ctx, 0) == '\r') { fail(ctx, "multiline raw string opener must end its line"); }
                    if advance(ctx) == '\n' { last_line = pos; }
                }
                if multiline {
                    for j in last_line..pos { if input[j] != ' ' && input[j] != '\t' { fail(ctx, "raw closing delimiter must start its line"); } }
                    let indent = input[last_line..pos];
                    var p = content;
                    while p < last_line {
                        let beginning = p;
                        while p < last_line && input[p] != '\n' { p++; }
                        var ending = p;
                        if ending > beginning && input[ending - 1] == '\r' { ending--; }
                        var trimmed = beginning;
                        var k = 0;
                        while k < indent.len && trimmed < ending && input[trimmed] == indent[k] { trimmed++; k++; }
                        if k < indent.len {
                            var blank = true;
                            for j in beginning..ending { if input[j] != ' ' && input[j] != '\t' { blank = false; } }
                            if !blank { fail(ctx, "raw string line lacks closing indentation"); }
                            trimmed = ending;
                        }
                        ctx.text_storage.append(input[trimmed..ending]);
                        p++;
                        if p < last_line { ctx.text_storage.push('\n'); }
                    }
                } else { ctx.text_storage.append(input[content..pos]); }
                advance(ctx); advance(ctx); advance(ctx);
            } else {
                while peek(ctx, 0) != c {
                    if pos >= input.len || peek(ctx, 0) == '\n' || peek(ctx, 0) == '\r' { fail(ctx, "unterminated quoted literal"); }
                    let ch = advance(ctx);
                    if ch == '\\' { ctx.text_storage.push(escape(ctx)); }
                    else { ctx.text_storage.push(ch); }
                }
                advance(ctx);
            }
            decoded_end = ctx.text_storage.len;
            kind = TK_STRING;
            if c == '\'' {
                if decoded_end - decoded != 1 { fail(ctx, "character literal must contain one byte"); }
                value = ctx.text_storage[decoded];
                kind = TK_INT;
                decoded = -1;
            }
        } else {
            var width = 1;
            // Longest match, independent of type/expression context.
            for op in ["<<=", ">>=", ".==", ".!=", "::", "..", "->", "=>", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ".=", "++", "--"] {
                if pos + op.len <= input.len && input[pos..pos + op.len] == op && op.len > width { width = op.len; }
            }
            if width == 1 {
                var valid = false;
                for ch in "(){}[];:,.?+-*/%!=<>&|^~" { if c == ch { valid = true; } }
                if !valid { fail(ctx, "unexpected source byte"); }
            }
            for j in width { advance(ctx); }
        }
        var spelling: const u8[:] = input[start..pos];
        if decoded >= 0 { spelling = ctx.text_storage[decoded..decoded_end]; }
        ctx.tokens.push(Token { kind: kind, text: spelling, file: file, offset: start, line: start_line, column: start_column, integer: value });
    }
    ctx.tokens.push(Token { kind: TK_END, text: "", file: file, offset: pos, line: line, column: column });
    ctx.sources[file].limit = ctx.tokens.len;
}
