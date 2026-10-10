// Hover text is built from declaration tokens and the comments next to them,
// not from a slice of the implementation body. Keep it editor-independent.
import std;
import.scan;
import.rpc;

struct SourceSymbol {
    start: i64, end: i64, range_start: i64, range_end: i64, kind: i64, signature_end: i64
}

fn symbol_matches(s: const u8[:], sym: SourceSymbol, name: const u8[:]) -> bool {
    var ts: SourceToken[>..] =[];
    scan_source(s[sym.start..sym.end], ts);
    var normalized: u8[>..] =[];
    for t in ts {
        if t.kind != COMMENT_TOKEN { normalized.append(token_text(s[sym.start..sym.end], t)); }
    }
    var reference: u8[>..] =[];
    var name_tokens: SourceToken[>..] =[];
    scan_source(name, name_tokens);
    for t in name_tokens {
        if t.kind != COMMENT_TOKEN { reference.append(token_text(name, t)); }
    }
    return normalized == reference;
}

fn qualified_identifier(s: const u8[:], t: SourceToken) -> bool {
    if t.kind != NAME_TOKEN { return false; }
    let text = token_text(s, t);
    for keyword in["fn", "thread_fn", "struct", "enum", "type", "namespace", "import", "let", "var", "const", "return", "break", "if", "else", "while", "for", "in", "as", "match", "guard", "extern", "export", "recursive"] {
        if text == keyword { return false; }
    }
    return true;
}

fn render_signature(out: u8[>..]&, s: const u8[:], sym: SourceSymbol) {
    let header = s[sym.range_start..sym.signature_end];
    var ts: SourceToken[>..] =[];
    scan_source(header, ts);
    var previous: const u8[:] = header[..0];
    var previous_kind = 0;
    var previous_binary = false;
    for i in ts.len {
        let t = ts[i];
        if t.kind == COMMENT_TOKEN { continue; }
        let text = token_text(header, t);
        let next = next_source_code(ts, i + 1);
        // Parameter and generic lists are shown without a trailing comma.
        if text == "," && next < ts.len &&(token_text(header, ts[next]) == ")" || token_text(header, ts[next]) == ">") {
            continue;
        }
        let binary =(text == "+" || text == "-" || text == "*") &&
        (previous_kind == NAME_TOKEN || previous_kind == NUMBER_TOKEN || previous_kind == LITERAL_TOKEN || previous == ")" || previous == "]");
        let closing = text == ")" || text == "]" || text == ">" || text == ">>" || text == "," || text == ";" || text == ":" || text == "." || text == "::" || text == "?" || text == "&" || text == "..";
        let opening = previous == "(" || previous == "[" || previous == "<" || previous == "::" || previous == "." || previous == "..";
        var space = false;
        if previous.len > 0 {
            if text == "::" && previous == ":" { space = true; }
            else if text == "(" {
                space = previous == "const" || previous == "->" || previous == "=" || previous == ":" || previous == ",";
            }
            else if text == "[" || text == "<" || closing || opening {
                /* type punctuation */
            }
            else if text == "=" || previous == "=" || text == "->" || previous == "->" || text == "=>" || previous == "=>" || binary || previous_binary ||
            previous == "," || previous == ":" || previous == "{" ||(previous_kind != PUNCT_TOKEN && t.kind != PUNCT_TOKEN) || text == "{" || text == "}" || previous == "}" {
                space = true;
            }
        }
        if space { out.push(' '); }
        out.append(text);
        previous = text;
        previous_kind = t.kind;
        previous_binary = binary;
    }
}

fn only_indentation(s: const u8[:], start: i64) -> bool {
    var i = start;
    while i > 0 && s[i - 1] != '\n' {
        i--;
        if s[i] != ' ' && s[i] != '\t' && s[i] != '\r' { return false; }
    }
    return true;
}

fn adjacent_comment_gap(s: const u8[:]) -> bool {
    var lines = 0;
    for c in s {
        if !source_space(c) { return false; }
        if c == '\n' { lines++; }
    }
    return lines <= 1;
}

fn append_comment_docs(out: u8[>..]&, comment: const u8[:]) {
    if starts_with(comment, "//") {
        var start = 2;
        if source_at(comment, start) == '/' || source_at(comment, start) == '!' { start++; }
        if source_at(comment, start) == ' ' { start++; }
        out.append(comment[start..]);
        return;
    }
    if comment.len < 4 { return; }
    var start = 2;
    if comment.len > 4 && comment[2] == '*' { start++; }
    let body = trim(comment[start..comment.len - 2]);
    let lines = split(body, '\n');
    for i in lines.len {
        if i > 0 { out.push('\n'); }
        let line = trim_end(lines[i]);
        let decorated = trim_start(line);
        if decorated.len > 0 && decorated[0] == '*' {
            var offset = 1;
            if source_at(decorated, offset) == ' ' { offset++; }
            out.append(decorated[offset..]);
        } else { out.append(line); }
    }
}

fn render_documentation(out: u8[>..]&, s: const u8[:], declaration_start: i64) {
    var ts: SourceToken[>..] =[];
    scan_source(s[..declaration_start], ts);
    var first = ts.len;
    var boundary = declaration_start;
    while first > 0 {
        let t = ts[first - 1];
        if t.kind != COMMENT_TOKEN || !only_indentation(s, t.start) || !adjacent_comment_gap(s[t.end..boundary]) {
            break;
        }
        first--;
        boundary = t.start;
    }
    for i in first..ts.len {
        if i > first { out.push('\n'); }
        append_comment_docs(out, token_text(s, ts[i]));
    }
}

fn client_hover_markdown(params: json::Node?) -> bool {
    let capabilities = json_member(params, "capabilities");
    let text_document = json_member(capabilities, "textDocument");
    let hover = json_member(text_document, "hover");
    let formats = json_member(hover, "contentFormat");
    guard formats else { return false; }
    if !json_is_array(formats) { return false; }
    var item = formats.first;
    while item {
        let kind = json_string(item);
        if kind == "markdown" { return true; }
        if kind == "plaintext" { return false; }
        item .= item.next;
    }
    return false;
}

fn render_symbol_hover(out: u8[>..]&, s: const u8[:], sym: SourceSymbol, markdown: bool) {
    var signature: u8[>..] =[];
    render_signature(signature, s, sym);
    // Choose a fence longer than any run in a default string value.
    var fence = 3;
    var run = 0;
    for c in signature {
        if c == '`' {
            run++;
            fence = max(fence, run + 1);
        } else { run = 0; }
    }
    if markdown {
        for i in fence { out.push('`'); }
        out.append("goose\n");
    }
    out.append(signature);
    if markdown {
        out.push('\n');
        for i in fence { out.push('`'); }
    }
    var docs: u8[>..] =[];
    render_documentation(docs, s, sym.range_start);
    if trim(docs).len > 0 {
        out.append(if markdown { "\n\n---\n\n" } else { "\n\n" });
        out.append(docs);
    }
}
