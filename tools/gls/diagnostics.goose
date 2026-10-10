// Diagnostics. The lexical ones are recomputed from the editor text on every
// change. The compiler's are computed when a file is opened or saved and kept
// until the next run. The compiler only sees saved files, so its result is
// hidden once the document has changed.
import std;
import os;
import.scan;
import.rpc;
import.protocol;
import.config;
import.check;

// One compiler diagnostic as JSON, with the file it belongs to. `key` names
// the run that produced it: its entry file or the file that was checked.
struct Batch {
    key: const u8[:], uri: const u8[:], json: const u8[:], version: i64, live: bool
}
var batches: Batch[>..] =[];
var semantic_enabled = true;
var reported_missing_compiler = false;

fn uri_hex(c: u8) -> u8 { if c < 10 { '0' + c } else { 'A' +(c - 10) } }

// The URI a client would use for a file that is not open. Open documents
// are matched by path instead, so their own spelling is used for them.
fn path_uri(path: const u8[:], out: u8[>..]&) {
    out.append("file://");
    if path.len > 0 && path[0] != '/' { out.push('/'); }
    for c in path {
        let plain =(c >= 'a' && c <= 'z') ||(c >= 'A' && c <= 'Z') ||(c >= '0' && c <= '9') ||
        c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
        if plain { out.push(c); }
        else if c == '\\' { out.push('/'); }
        else {
            out.push('%');
            out.push(uri_hex(c >> 4));
            out.push(uri_hex(c & 15));
        }
    }
}

fn open_document_for_path(path: const u8[:]) -> i64 {
    for i in documents.len {
        if !documents[i].open { continue; }
        var name: u8[>..] =[];
        uri_filename(documents[i].uri, name);
        if name == path { return i; }
    }
    return -1;
}

fn uri_for_path(path: const u8[:], out: u8[>..]&) {
    let doc = open_document_for_path(path);
    if doc >= 0 { out.append(documents[doc].uri); } else { path_uri(path, out); }
}

// The bytes of a file as the editor sees it: the open text, or the disk.
fn file_text(path: const u8[:], buffer: u8[>..]&) -> const u8[:] {
    let doc = open_document_for_path(path);
    if doc >= 0 { return documents[doc].text; }
    if !read_file(path, buffer) { buffer.clear(); }
    return buffer;
}

// Byte offsets of the part of a line a finding covers.
fn finding_range(text: const u8[:], line: i64, column: i64) -> i64, i64 {
    var start = 0;
    var row = 0;
    while row < line && start < text.len {
        if text[start] == '\n' { row++; }
        start++;
    }
    if row < line { return text.len, text.len; }
    var end = start;
    while end < text.len && text[end] != '\n' && text[end] != '\r' { end++; }
    if column >= 0 {
        let at = min(start + column, end);
        var stop = at;
        while stop < end &&(source_name(text[stop]) || source_digit(text[stop])) { stop++; }
        if stop == at && stop < end { stop += utf8_width(text[stop]); }
        return at, min(stop, end);
    }
    var first = start;
    while first < end && source_space(text[first]) { first++; }
    return first, end;
}

fn finding_json(out: u8[>..]&, text: const u8[:], f: Finding, frames: const Frame[:]) {
    let from, to = finding_range(text, f.line, f.column);
    out.append("{\"range\":");
    range_json(out, text, from, to);
    format(out, ",\"severity\":", if f.warning { 2 } else { 1 }, ",\"source\":\"Goose\",\"message\":");
    json_quote(out, f.message);
    if f.frames > 0 {
        out.append(",\"relatedInformation\":[");
        for k in f.frames {
            let frame = frames[f.first_frame + k];
            var uri: u8[>..] =[];
            uri_for_path(frame.path, uri);
            if k > 0 { out.push(','); }
            out.append("{\"location\":{\"uri\":");
            json_quote(out, uri);
            format(out, ",\"range\":{\"start\":{\"line\":", max(frame.line, 0), ",\"character\":0},\"end\":{\"line\":", max(frame.line, 0), ",\"character\":0}}},\"message\":");
            json_quote(out, str("Instantiated ", frame.name));
            out.push('}');
        }
        out.push(']');
    }
    out.push('}');
}

fn publish_list(uri: const u8[:], version: i64, syntax_error: i64, text: const u8[:], open: bool) {
    var out: u8[>..] =[];
    out.append("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":");
    json_quote(out, uri);
    if open { format(out, ",\"version\":", version); }
    out.append(",\"diagnostics\":[");
    var first = true;
    if syntax_error >= 0 {
        out.append("{\"range\":");
        range_json(out, text, syntax_error, min(syntax_error + 1, text.len));
        out.append(",\"severity\":1,\"source\":\"Goose syntax\",\"message\":\"Incomplete literal, mismatched delimiter, or unexpected character\"}");
        first = false;
    }
    for b in batches {
        if !b.live || b.uri != uri ||(open && b.version >= 0 && b.version != version) { continue; }
        if !first { out.push(','); }
        first = false;
        out.append(b.json);
    }
    out.append("]}}");
    send_message(out);
}

fn publish_syntax(doc: i64) {
    var error = -1;
    if documents[doc].open {
        var ts: SourceToken[>..] =[];
        error = scan_source(documents[doc].text, ts);
    }
    publish_list(documents[doc].uri, documents[doc].version, error, documents[doc].text, documents[doc].open);
}

// Republishes a file the compiler reported on, open or not.
fn publish_uri(uri: const u8[:]) {
    let doc = document_index(uri);
    if doc >= 0 { publish_syntax(doc); } else { publish_list(uri, 0, -1, "", false); }
}

fn drop_batches(key: const u8[:], touched: u8[>..]&) {
    for i in batches.len {
        if !batches[i].live || batches[i].key != key { continue; }
        touched.append(batches[i].uri);
        touched.push(0);
        release_document_text(batches[i].key);
        release_document_text(batches[i].uri);
        release_document_text(batches[i].json);
        batches[i].live = false;
    }
}

fn add_batch(key: const u8[:], uri: const u8[:], json: const u8[:], version: i64) {
    let batch = Batch {
        key: own_document_text(key), uri: own_document_text(uri), json: own_document_text(json), version: version, live: true
    };
    for i in batches.len {
        if !batches[i].live {
            batches[i] = batch;
            return;
        }
    }
    batches.push(batch);
}

fn show_message(kind: i64, message: const u8[:]) {
    var out: u8[>..] =[];
    format(out, "{\"jsonrpc\":\"2.0\",\"method\":\"window/showMessage\",\"params\":{\"type\":", kind, ",\"message\":");
    json_quote(out, message);
    out.append("}}");
    send_message(out);
}

fn add_finding_batch(key: const u8[:], path: const u8[:], f: Finding, frames: const Frame[:]) {
    var buffer: u8[>..] =[];
    var uri: u8[>..] =[];
    uri_for_path(path, uri);
    var json: u8[>..] =[];
    finding_json(json, file_text(path, buffer), f, frames);
    let doc = open_document_for_path(path);
    add_batch(key, uri, json, if doc >= 0 { documents[doc].version } else { -1 });
}

// Runs the compiler on `path` (a saved file) and replaces what its previous
// run reported. Settings come from the nearest gls.json.
fn check_saved_file(path: const u8[:]) {
    if !semantic_enabled || path.len == 0 { return; }
    var settings = CheckSettings {};
    var compiler: u8[>..] =[];
    var stdlib: u8[>..] =[];
    var entry: u8[>..] =[];
    var problem: u8[>..] =[];
    var touched: u8[>..] =[];
    if !check_config(path, settings, compiler, stdlib, entry, problem) {
        show_message(2, str("Goose check settings ignored: ", problem));
        settings = CheckSettings {};
        compiler.clear();
        stdlib.clear();
        entry.clear();
    }
    let target: const u8[:] = if entry.len > 0 { entry } else { path };
    if !settings.enabled {
        drop_batches(target, touched);
        republish(touched);
        return;
    }
    var output: u8[>..] =[];
    let code = run_check(path, settings, compiler, stdlib, entry, output);
    if code == -1 {
        if !reported_missing_compiler {
            reported_missing_compiler = true;
            show_message(2, "Goose compiler not found, so only syntax errors are reported. Put goose on PATH, or set GOOSE_COMPILER or \"check\": {\"compiler\": ...} in gls.json.");
        }
        drop_batches(target, touched);
        republish(touched);
        return;
    }
    var findings: Finding[>..] =[];
    var frames: Frame[>..] =[];
    parse_compiler_output(output, findings, frames);
    drop_batches(target, touched);
    var errors = 0;
    for f in findings {
        if !f.warning { errors++; }
        var absolute: u8[>..] =[];
        if !starts_with(f.path, "/") && !(f.path.len > 1 && f.path[1] == ':') {
            if working_directory(absolute) { absolute.push('/'); }
        }
        absolute.append(f.path);
        add_finding_batch(target, absolute, f, frames);
        touched.append(absolute);
        touched.push(0);
    }
    if code != 0 && errors == 0 {
        // The compiler failed without saying where: show it on the checked file.
        var message: u8[>..] =[];
        if code == -2 {
            format(message, "Goose check exceeded ", settings.timeout_ms, " ms and was stopped.");
        }
        else { failure_message(output, message); }
        if message.len == 0 { format(message, "Goose check failed with exit status ", code); }
        let unlocated = Finding {
            path: path, line: 0, column: -1, warning: false, message: message, first_frame: 0, frames: 0
        };
        add_finding_batch(target, path, unlocated, frames);
        touched.append(path);
        touched.push(0);
    }
    republish(touched);
}

// Publishes every file named in a NUL-separated list, each once.
fn republish(touched: const u8[:]) {
    var done: u8[>..] =[];
    var start = 0;
    while start < touched.len {
        var end = start;
        while end < touched.len && touched[end] != 0 { end++; }
        let name = touched[start..end];
        start = end + 1;
        if name.len == 0 { continue; }
        var uri: u8[>..] =[];
        // Batch URIs are kept as published. Finding paths are kept as the compiler wrote them.
        if starts_with(name, "file://") { uri.append(name); } else { uri_for_path(name, uri); }
        var seen = false;
        var at = 0;
        while at < done.len {
            var stop = at;
            while stop < done.len && done[stop] != 0 { stop++; }
            if done[at..stop] == uri { seen = true; }
            at = stop + 1;
        }
        if seen { continue; }
        done.append(uri);
        done.push(0);
        publish_uri(uri);
    }
}

// Forgets what a closed document's run reported, unless an entry file keeps it.
fn forget_document(path: const u8[:]) {
    var touched: u8[>..] =[];
    drop_batches(path, touched);
    republish(touched);
}
