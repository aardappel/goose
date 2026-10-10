// Open documents and the protocol pieces every handler needs. Positions are
// byte offsets inside, and UTF-16 only at the protocol boundary.
import std;
import os;
import.scan;
import.rpc;

struct OpenDocument {
    uri: const u8[:], text: const u8[:], version: i64, open: bool
}
var documents: OpenDocument[>..] =[];
reusable[] var document_bytes: u8[>..] =[];
var initialized = false;
var shutting_down = false;
var hover_markdown = false;

fn own_document_text(s: const u8[:]) -> const u8[:] {
    let bytes = document_bytes.alloc_slice(s.len);
    for i in s.len { bytes[i] = s[i]; }
    return bytes[..s.len];
}

fn release_document_text(s: const u8[:]) { document_bytes.free_slice(s); }

fn document_index(uri: const u8[:]) -> i64 {
    for i in documents.len {
        if documents[i].open && documents[i].uri == uri { return i; }
    }
    return -1;
}

// Byte offsets internally, UTF-16 positions at the protocol boundary.
fn utf8_width(c: u8) -> i64 {
    if c < 128 { return 1; }
    if c < 224 { return 2; }
    if c < 240 { return 3; }
    return 4;
}

fn position_json(out: u8[>..]&, s: const u8[:], offset: i64) {
    var line = 0;
    var character = 0;
    var i = 0;
    while i < min(offset, s.len) {
        if s[i] == '\n' {
            line++;
            character = 0;
            i++;
        }
        else {
            let width = utf8_width(s[i]);
            character += if width == 4 { 2 } else { 1 };
            i += width;
        }
    }
    format(out, "{\"line\":", line, ",\"character\":", character, "}");
}

fn position_offset(s: const u8[:], line: i64, character: i64) -> i64 {
    var l = 0;
    var c = 0;
    var i = 0;
    while i < s.len {
        if l == line && c >= character { return i; }
        if s[i] == '\n' {
            if l == line { return i; }
            l++;
            c = 0;
            i++;
        }
        else {
            let width = utf8_width(s[i]);
            c += if width == 4 { 2 } else { 1 };
            i += width;
        }
    }
    return s.len;
}

fn range_json(out: u8[>..]&, s: const u8[:], start: i64, end: i64) {
    out.append("{\"start\":");
    position_json(out, s, start);
    out.append(",\"end\":");
    position_json(out, s, end);
    out.push('}');
}

fn send_message(body: const u8[:]) {
    write_stdout(str("Content-Length: ", body.len, "\r\n\r\n"));
    write_stdout(body);
    flush_stdout();
}

fn response(id: const u8[:], result: const u8[:]) {
    send_message(str("{\"jsonrpc\":\"2.0\",\"id\":", id, ",\"result\":", result, "}"));
}

fn rpc_error(id: const u8[:], code: i64, message: const u8[:]) {
    var out: u8[>..] =[];
    format(out, "{\"jsonrpc\":\"2.0\",\"id\":", id, ",\"error\":{\"code\":", code, ",\"message\":");
    json_quote(out, message);
    out.append("}}");
    send_message(out);
}
