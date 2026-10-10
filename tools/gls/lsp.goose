// A stdio LSP server. It keeps the full text of open documents in memory.
// Formatting never reads or writes the editor's file on disk.
import std;
import os;
import.scan;
import.rpc;
import.protocol;
import.diagnostics;
import.config;
import.formatter;
import.hover;
import.workspace;

// The namespace a declaration is in: the name of the last `namespace` before it.
fn symbol_namespace(s: const u8[:], syms: const SourceSymbol[:], index: i64) -> const u8[:] {
    var name: const u8[:] = s[..0];
    for i in index { if syms[i].kind == 3 { name = s[syms[i].start..syms[i].end]; } }
    return name;
}

// Whether declaration `index` of an imported file is what `wanted` names.
// Outside a namespace that is the bare name. Inside one it is `ns::name`.
fn imported_match(s: const u8[:], syms: const SourceSymbol[:], index: i64, wanted: const u8[:]) -> bool {
    if syms[index].kind == 3 { return false; }
    let name = symbol_namespace(s, syms, index);
    if name.len == 0 { return symbol_matches(s, syms[index], wanted); }
    return starts_with(wanted, name) && starts_with(wanted[name.len..], "::") && symbol_matches(s, syms[index], wanted[name.len + 2..]);
}

// The files the document imports (file 0 is the document itself).
fn load_imports(uri: const u8[:], source: const u8[:], files: WorkspaceFile[>..]&, names: u8[>..]&, texts: u8[>..]&) {
    var path: u8[>..] =[];
    uri_filename(uri, path);
    if path.len > 0 { index_imports(path, source, files, names, texts); }
}

// Files on disk only. Other URI schemes (untitled buffers) have no path to compile.
fn check_document(uri: const u8[:]) {
    var path: u8[>..] =[];
    uri_filename(uri, path);
    if path.len > 0 { check_saved_file(path); }
}

fn source_symbols(s: const u8[:], out: SourceSymbol[>..]&) {
    var ts: SourceToken[>..] =[];
    scan_source(s, ts);
    var depth = 0;
    var i = 0;
    while i < ts.len {
        let word = token_text(s, ts[i]);
        if ts[i].kind != PUNCT_TOKEN && ts[i].kind != NAME_TOKEN {
            i++;
            continue;
        }
        if word == "{" { depth++; }
        if word == "}" { depth--; }
        var kind = 0;
        if word == "fn" || word == "thread_fn" { kind = 12; }
        if word == "struct" { kind = 23; }
        if word == "enum" { kind = 10; }
        if word == "type" { kind = 26; }
        if word == "namespace" { kind = 3; }
        if word == "let" || word == "const" { kind = 14; }
        if word == "var" { kind = 13; }
        if depth != 0 || kind == 0 {
            i++;
            continue;
        }
        var n = next_source_code(ts, i + 1);
        let name_begin = n;
        if n < ts.len && token_text(s, ts[n]) == "::" { n = next_source_code(ts, n + 1); }
        if n >= ts.len || ts[n].kind != NAME_TOKEN {
            i++;
            continue;
        }
        let start = ts[name_begin].start;
        var end = ts[n].end;
        n = next_source_code(ts, n + 1);
        while n < ts.len && token_text(s, ts[n]) == "::" {
            let part = next_source_code(ts, n + 1);
            if part >= ts.len || ts[part].kind != NAME_TOKEN { break; }
            end = ts[part].end;
            n = next_source_code(ts, part + 1);
        }
        var nesting = 0;
        var range_end = s.len;
        var signature_end = s.len;
        var signature_found = false;
        var j = n;
        while j < ts.len {
            let text = token_text(s, ts[j]);
            if ts[j].kind == PUNCT_TOKEN {
                if nesting == 0 && !signature_found &&(text == "{" || text == ";" ||
                (text == "=" &&(word == "let" || word == "var" || word == "const"))) {
                    signature_end = ts[j].start;
                    signature_found = true;
                }
                if text == "{" || text == "(" || text == "[" { nesting++; }
                if text == "}" || text == ")" || text == "]" {
                    nesting--;
                    if text == "}" && nesting == 0 &&(kind == 12 || kind == 23 || kind == 10) {
                        range_end = ts[j].end;
                        break;
                    }
                }
                if text == ";" && nesting == 0 {
                    range_end = ts[j].end;
                    break;
                }
            }
            j++;
        }
        let first = declaration_prefix(s, ts, i);
        out.push(SourceSymbol {
            start: start, end: end, range_start: ts[first].start, range_end: range_end, kind: kind, signature_end: signature_end
        });
        // Skip declaration bodies, including fn types in aliases and initializers.
        i = j + 1;
    }
}

fn symbol_json(out: u8[>..]&, s: const u8[:], sym: SourceSymbol) {
    out.append("{\"name\":");
    json_quote(out, s[sym.start..sym.end]);
    format(out, ",\"kind\":", sym.kind, ",\"range\":");
    range_json(out, s, sym.range_start, sym.range_end);
    out.append(",\"selectionRange\":");
    range_json(out, s, sym.start, sym.end);
    out.push('}');
}

fn format_edits(out: u8[>..]&, source: const u8[:], options: json::Node?, uri: const u8[:], error: u8[>..]&) -> bool {
    let width = clamp(json_integer(json_member(options, "tabSize"), 4), 1, 16);
    let spaces = json_bool(json_member(options, "insertSpaces"), true);
    var settings = FormatterOptions { indent_width: width, insert_spaces: spaces };
    var filename: u8[>..] =[];
    uri_filename(uri, filename);
    if !formatter_config(filename, settings, error) { return false; }
    var formatted: u8[>..] =[];
    if !format_source(source, formatted, settings) { return false; }
    if source == formatted {
        out.append("[]");
        return true;
    }
    out.append("[{\"range\":");
    range_json(out, source, 0, source.len);
    out.append(",\"newText\":");
    json_quote(out, formatted);
    out.append("}]");
    return true;
}

fn handle_message(message: const u8[:]) {
    var strings: u8[>..] =[];
    var nodes: json::Node[>..] =[];
    let root, parse_error = json::parse(message, nodes, strings);
    if !root {
        rpc_error("null", -32700, "Parse error");
        return;
    }
    let method_node = json_member(root, "method");
    let id_node = json_member(root, "id");
    var id_bytes: u8[>..] =[];
    json_raw_id(id_node, id_bytes);
    let id: const u8[:] = id_bytes;
    let method, has_method = json::as_string(method_node);
    if json_string(json_member(root, "jsonrpc")) != "2.0" || !has_method {
        rpc_error(id, -32600, "Invalid Request");
        return;
    }
    let params = json_member(root, "params");
    if method == "exit" {
        exit(if shutting_down { 0 } else { 1 });
    }
    if method == "initialize" {
        if initialized || !id_node {
            rpc_error(id, -32600, "Already initialized or missing request id");
            return;
        }
        initialized = true;
        hover_markdown = client_hover_markdown(params);
        let options = json_member(params, "initializationOptions");
        if !json_bool(json_member(options, "semanticDiagnostics"), true) {
            semantic_enabled = false;
        }
        response(id, "{\"capabilities\":{\"positionEncoding\":\"utf-16\",\"textDocumentSync\":{\"openClose\":true,\"change\":1,\"willSaveWaitUntil\":true,\"save\":true},\"documentFormattingProvider\":true,\"documentSymbolProvider\":true,\"completionProvider\":{},\"hoverProvider\":true,\"definitionProvider\":true},\"serverInfo\":{\"name\":\"Goose (written in Goose)\",\"version\":\"0.1.0\"}}");
        return;
    }
    if !initialized {
        if id_node { rpc_error(id, -32002, "Server not initialized"); }
        return;
    }
    if shutting_down {
        if id_node { rpc_error(id, -32600, "Server is shutting down"); }
        return;
    }
    if method == "shutdown" {
        shutting_down = true;
        if id_node { response(id, "null"); }
        return;
    }
    let td = json_member(params, "textDocument");
    let uri = json_string(json_member(td, "uri"));
    var doc = document_index(uri);
    if method == "textDocument/didOpen" {
        if uri.len == 0 { return; }
        if doc >= 0 {
            release_document_text(documents[doc].text);
            release_document_text(documents[doc].uri);
            documents[doc].open = false;
        }
        doc = -1;
        for i in documents.len {
            if !documents[i].open {
                doc = i;
                break;
            }
        }
        let own_uri = own_document_text(uri);
        let own_text = own_document_text(json_string(json_member(td, "text")));
        let version = json_integer(json_member(td, "version"), 0);
        if doc < 0 {
            doc = documents.len;
            documents.push(OpenDocument {
                uri: own_uri, text: own_text, version: version, open: true
            });
        } else {
            documents[doc] = OpenDocument {
                uri: own_uri, text: own_text, version: version, open: true
            };
        }
        publish_syntax(doc);
        check_document(uri);
        return;
    }
    if method == "textDocument/didSave" {
        if doc >= 0 { check_document(uri); }
        return;
    }
    if method == "textDocument/didChange" {
        if doc < 0 { return; }
        let version = json_integer(json_member(td, "version"), documents[doc].version + 1);
        if version <= documents[doc].version { return; }
        let changes = json_member(params, "contentChanges");
        guard changes else { return; }
        if !json_is_array(changes) { return; }
        var change = changes.first;
        while change {
            // Full sync was agreed on, so a ranged change would put us out of sync.
            if json_member(change, "range") { return; }
            let text = json_member(change, "text");
            let new_text, is_text = json::as_string(text);
            if !is_text { return; }
            release_document_text(documents[doc].text);
            documents[doc].text = own_document_text(new_text);
            change .= change.next;
        }
        documents[doc].version = version;
        publish_syntax(doc);
        return;
    }
    if method == "textDocument/didClose" {
        if doc >= 0 {
            var path: u8[>..] =[];
            uri_filename(documents[doc].uri, path);
            documents[doc].open = false;
            publish_syntax(doc);
            forget_document(path);
            release_document_text(documents[doc].text);
            release_document_text(documents[doc].uri);
        }
        return;
    }
    if !id_node { return; }
    let supported = method == "textDocument/formatting" || method == "textDocument/willSaveWaitUntil" ||
    method == "textDocument/documentSymbol" || method == "textDocument/completion" || method == "textDocument/hover" || method == "textDocument/definition";
    if !supported {
        rpc_error(id, -32601, "Method not found");
        return;
    }
    if doc < 0 {
        rpc_error(id, -32602, "Document is not open");
        return;
    }
    let source = documents[doc].text;
    var out: u8[>..] =[];
    if method == "textDocument/formatting" || method == "textDocument/willSaveWaitUntil" {
        var error: u8[>..] =[];
        if !format_edits(out, source, json_member(params, "options"), uri, error) {
            rpc_error(id, -32602, if error.len > 0 { error } else {
                "Incomplete source or formatting would change tokens"
            });
            return;
        }
        response(id, out);
        return;
    }
    var syms: SourceSymbol[>..] =[];
    source_symbols(source, syms);
    if method == "textDocument/documentSymbol" {
        out.push('[');
        for i in syms.len {
            if i > 0 { out.push(','); }
            symbol_json(out, source, syms[i]);
        }
        out.push(']');
        response(id, out);
        return;
    }
    if method == "textDocument/completion" {
        out.append("{\"isIncomplete\":false,\"items\":[");
        var first = true;
        for word in["import", "namespace", "struct", "enum", "type", "fn", "thread_fn", "export", "recursive", "let", "var", "extern", "const", "pad", "reusable", "if", "else", "while", "for", "in", "loop", "block", "guard", "match", "return", "break", "continue", "as", "true", "false", "null", "self", "bool", "varint", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64"] {
            if !first { out.push(','); }
            first = false;
            out.append("{\"label\":");
            json_quote(out, word);
            out.append(",\"kind\":14}");
        }
        for sym in syms {
            out.append(",{\"label\":");
            json_quote(out, source[sym.start..sym.end]);
            format(out, ",\"kind\":", if sym.kind == 12 { 3 } else { 6 }, "}");
        }
        var files: WorkspaceFile[>..] =[];
        var names: u8[>..] =[];
        var texts: u8[>..] =[];
        load_imports(uri, source, files, names, texts);
        for j in 1..files.len {
            let text = texts[files[j].text_start..files[j].text_end];
            var other: SourceSymbol[>..] =[];
            source_symbols(text, other);
            for k in other.len {
                if other[k].kind == 3 { continue; }
                let name = symbol_namespace(text, other, k);
                out.append(",{\"label\":");
                var label: u8[>..] =[];
                if name.len > 0 { format(label, name, "::"); }
                label.append(text[other[k].start..other[k].end]);
                json_quote(out, label);
                format(out, ",\"kind\":", if other[k].kind == 12 { 3 } else { 6 }, "}");
            }
        }
        out.append("]}");
        response(id, out);
        return;
    }
    let position = json_member(params, "position");
    let offset = position_offset(source, json_integer(json_member(position, "line"), 0), json_integer(json_member(position, "character"), 0));
    var start = offset;
    var end = offset;
    while start > 0 &&(source_name(source[start - 1]) || source_digit(source[start - 1]) || source[start - 1] == ':') {
        start--;
    }
    while end < source.len &&(source_name(source[end]) || source_digit(source[end]) || source[end] == ':') {
        end++;
    }
    // Only look at identifiers, never at apparent names inside comments or literals.
    var ts: SourceToken[>..] =[];
    scan_source(source, ts);
    var identifier = false;
    for i in ts.len {
        let t = ts[i];
        if !qualified_identifier(source, t) || offset < t.start || offset > t.end { continue; }
        identifier = true;
        var left = i;
        var right = i;
        loop {
            let separator = previous_source_code(ts, left - 1);
            if separator < 0 || token_text(source, ts[separator]) != "::" { break; }
            let word = previous_source_code(ts, separator - 1);
            if word < 0 || !qualified_identifier(source, ts[word]) {
                left = separator;
                break;
            }
            left = word;
        }
        loop {
            let separator = next_source_code(ts, right + 1);
            if separator >= ts.len || token_text(source, ts[separator]) != "::" { break; }
            let word = next_source_code(ts, separator + 1);
            if word >= ts.len || !qualified_identifier(source, ts[word]) { break; }
            right = word;
        }
        start = ts[left].start;
        end = ts[right].end;
        break;
    }
    if identifier && method == "textDocument/hover" {
        var content: u8[>..] =[];
        for sym in syms {
            if !symbol_matches(source, sym, source[start..end]) { continue; }
            if content.len > 0 {
                content.append(if hover_markdown { "\n\n---\n\n" } else { "\n\n" });
            }
            render_symbol_hover(content, source, sym, hover_markdown);
        }
        var files: WorkspaceFile[>..] =[];
        var names: u8[>..] =[];
        var texts: u8[>..] =[];
        load_imports(uri, source, files, names, texts);
        for j in 1..files.len {
            let text = texts[files[j].text_start..files[j].text_end];
            var other: SourceSymbol[>..] =[];
            source_symbols(text, other);
            for k in other.len {
                if !imported_match(text, other, k, source[start..end]) { continue; }
                if content.len > 0 {
                    content.append(if hover_markdown { "\n\n---\n\n" } else { "\n\n" });
                }
                render_symbol_hover(content, text, other[k], hover_markdown);
            }
        }
        if content.len > 0 {
            out.append("{\"contents\":{\"kind\":");
            json_quote(out, if hover_markdown { "markdown" } else { "plaintext" });
            out.append(",\"value\":");
            json_quote(out, content);
            out.append("},\"range\":");
            range_json(out, source, start, end);
            out.push('}');
            response(id, out);
            return;
        }
    }
    if identifier && method == "textDocument/definition" {
        for sym in syms {
            if !symbol_matches(source, sym, source[start..end]) { continue; }
            out.append("{\"uri\":");
            json_quote(out, uri);
            out.append(",\"range\":");
            range_json(out, source, sym.start, sym.end);
            out.push('}');
            response(id, out);
            return;
        }
        var files: WorkspaceFile[>..] =[];
        var names: u8[>..] =[];
        var texts: u8[>..] =[];
        load_imports(uri, source, files, names, texts);
        for j in 1..files.len {
            let text = texts[files[j].text_start..files[j].text_end];
            var other: SourceSymbol[>..] =[];
            source_symbols(text, other);
            for k in other.len {
                if !imported_match(text, other, k, source[start..end]) { continue; }
                var target: u8[>..] =[];
                uri_for_path(names[files[j].name_start..files[j].name_end], target);
                out.append("{\"uri\":");
                json_quote(out, target);
                out.append(",\"range\":");
                range_json(out, text, other[k].start, other[k].end);
                out.push('}');
                response(id, out);
                return;
            }
        }
    }
    response(id, "null");
}

fn serve_lsp() {
    loop {
        var length = -1;
        var headers = 0;
        loop {
            var line: u8[>..] =[];
            if !read_line(line) { return; }
            headers += line.len;
            if headers > 8192 {
                write_stderr("LSP headers too large\n");
                exit(1);
            }
            if line.len == 0 { break; }
            let colon = find(line, ":");
            if colon >= 0 && lsp_header_equal(line[..colon], "Content-Length") {
                if length >= 0 {
                    write_stderr("duplicate Content-Length\n");
                    exit(1);
                }
                let number, ok = parse_int(trim(line[colon + 1..]));
                if !ok {
                    write_stderr("invalid Content-Length\n");
                    exit(1);
                }
                length = number;
            }
        }
        if length < 0 || length > 16777216 {
            write_stderr("invalid or oversized LSP frame\n");
            exit(1);
        }
        var message: u8[>..] =[];
        if !read_stdin_bytes(length, message) {
            write_stderr("truncated LSP frame\n");
            exit(1);
        }
        handle_message(message);
    }
}

fn lsp_header_equal(a: const u8[:], b: const u8[:]) -> bool {
    if a.len != b.len { return false; }
    for i in a.len {
        if (a[i] | 32) !=(b[i] | 32) { return false; }
    }
    return true;
}
