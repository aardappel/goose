// A JSON parser and printer. The document becomes a tree of variable-mode
// enum values in one grow-only pool: a number is a tag and an f64, a
// string is a tag and its bytes, an array is a tag, a count and a link to
// its first child. Every link is a 4-byte self-relative offset within the
// pool, so the parsed document is a single contiguous block that could be
// copied or saved as it is. Children are parsed (and pushed) before their
// parent, and siblings are chained by patching the previous child's link.
// The parser's state -- the text, the cursor, a key buffer -- is a set of
// locals of `parse`, and the functions nested inside it, the recursive
// ones included, use them as free variables. Any syntax error, however
// deep, is one `return ... from parse` carrying the message and position
// back to the top.
//
// Build and run (from this directory):
//   goose -o json.c 18_json.goose && cl json.c && json [file.json]

import std;
import os;

// A node is a sibling link, a key (empty for array elements) and the
// value; the value's payload is exactly as large as its variant needs.
struct Node { next: Node&<u32>?, key: u8[varint], val: Json.. }

enum Json {
    Null,
    Bool { v: bool },
    Num { v: f64 },
    Str { s: u8[varint] },
    Arr { first: Node&<u32>?, count: varint },
    Obj { first: Node&<u32>?, count: varint },
}

var pool: Node[>..] = [];

// --- parsing -----------------------------------------------------------

// The root node, or null and a message. A function in the recursive cycle
// cannot own a growable value, but the cycle lives inside this function,
// whose locals outlive every activation of it: the key buffer is one.
fn parse(text: u8[:]) -> Node?, u8[>..] {
    var pos = 0;
    var keys: u8[>..] = [];              // object keys, decoded before their values

    fn skip_space() {
        while pos < text.len && (text[pos] == ' ' || text[pos] == '\n' || text[pos] == '\r' || text[pos] == '\t') { pos++; }
    }

    fn fail(what: u8[:]) -> u8[>..] { str(what, " at byte ", pos) }

    fn expect(c: u8) {
        skip_space();
        if pos < text.len && text[pos] == c { pos++; return; }
        var msg: u8[>..] = [];
        format(msg, "expected '");
        msg.push(c);                     // the byte itself: str(c) would print its number
        msg.push('\'');
        return null, fail(msg) from parse;
    }

    fn hex_digit(c: u8) -> i64 {
        if c >= '0' && c <= '9' { return c - '0'; }
        if c >= 'a' && c <= 'f' { return c - 'a' + 10; }
        if c >= 'A' && c <= 'F' { return c - 'A' + 10; }
        return null, fail("bad hex digit") from parse;
    }

    // A string's decoded bytes, built straight into the field that receives
    // them. Not part of the recursive cycle, so it may own a builder.
    fn parse_string() -> u8[>..] {
        expect('"');
        var s: u8[>..] = [];
        loop {
            guard pos < text.len else { return null, fail("unterminated string") from parse; }
            let c = text[pos];
            pos++;
            if c == '"' { break; }
            if c != '\\' { s.push(c); continue; }
            guard pos < text.len else { return null, fail("bad escape") from parse; }
            let e = text[pos];
            pos++;
            if e == 'n' { s.push('\n'); }
            else if e == 't' { s.push('\t'); }
            else if e == 'r' { s.push('\r'); }
            else if e == 'u' {
                guard pos + 4 <= text.len else { return null, fail("bad \\u escape") from parse; }
                var cp = 0;
                for i in 4 { cp = cp * 16 + hex_digit(text[pos + i]); }
                pos += 4;
                push_utf8(s, cp);
            }
            else { s.push(e); }          // \" \\ \/ and the rest
        }
        return s;
    }

    fn parse_number() -> f64 {
        let start = pos;
        while pos < text.len && ((text[pos] >= '0' && text[pos] <= '9') || text[pos] == '-' ||
                                 text[pos] == '+' || text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E') { pos++; }
        let v, ok = parse_flt(text[start..pos]);
        guard ok else { return null, fail("bad number") from parse; }
        return v;
    }

    fn parse_word(w: u8[:]) {
        guard pos + w.len <= text.len && text[pos..pos + w.len] == w else { return null, fail("unexpected token") from parse; }
        pos += w.len;
    }

    // One child of a container: an object member (key, colon, value) or an
    // array element. The key is decoded into `keys` and handed down as a
    // slice of it; the node copies its bytes.
    recursive fn parse_child(is_obj: bool) -> Node& {
        if is_obj {
            let start = keys.len;
            keys.append(parse_string());
            expect(':');
            return parse_value(keys[start..]);
        }
        return parse_value("");
    }

    // One value, pushed onto the pool as a node with the given key; the
    // node it returns is what the caller links into its own chain.
    recursive fn parse_value(key: u8[:]) -> Node& {
        skip_space();
        guard pos < text.len else { return null, fail("unexpected end of input") from parse; }
        let c = text[pos];
        if c == '{' || c == '[' {
            let is_obj = c == '{';
            let close = if is_obj { '}' } else { ']' };
            pos++;
            skip_space();
            guard pos < text.len else { return null, fail("unclosed container") from parse; }
            var first: Node? = null;
            var count = 0;
            if text[pos] != close {
                // The first child, then the rest, each linked from the
                // previous one: the link is a fixed-width relative
                // reference, so it can be stored after the node was pushed.
                let head .= parse_child(is_obj);
                first .= head;
                var last .= head;
                count = 1;
                loop {
                    skip_space();
                    guard pos < text.len else { return null, fail("unclosed container") from parse; }
                    if text[pos] == close { break; }
                    expect(',');
                    let child .= parse_child(is_obj);
                    last.next .= child;
                    last .= child;
                    count++;
                }
            }
            pos++;
            if is_obj { return pool.push(Node { next: null, key: key, val: Json.Obj { first: first, count: count } }); }
            return pool.push(Node { next: null, key: key, val: Json.Arr { first: first, count: count } });
        }
        // A leaf is one push: the node is built at the end of the pool, the
        // decoded string straight into its payload, the key's bytes copied
        // in -- no intermediate node value anywhere.
        if c == '"' { return pool.push(Node { next: null, key: key, val: Json.Str { s: parse_string() } }); }
        if c == 't' { parse_word("true"); return pool.push(Node { next: null, key: key, val: Json.Bool { v: true } }); }
        if c == 'f' { parse_word("false"); return pool.push(Node { next: null, key: key, val: Json.Bool { v: false } }); }
        if c == 'n' { parse_word("null"); return pool.push(Node { next: null, key: key, val: Json.Null }); }
        return pool.push(Node { next: null, key: key, val: Json.Num { v: parse_number() } });
    }

    let root .= parse_value("");
    skip_space();
    guard pos == text.len else { return null, fail("trailing input") from parse; }
    return root, str("");
}

// --- printing ----------------------------------------------------------

fn quote(out: u8[>..]&, s: u8[:]) {
    out.push('"');
    for c in s {
        if c == '"' || c == '\\' { out.push('\\'); out.push(c); }
        else if c == '\n' { out.append("\\n"); }
        else if c == '\t' { out.append("\\t"); }
        else { out.push(c); }
    }
    out.push('"');
}

fn newline(out: u8[>..]&, indent: i64) {
    out.push('\n');
    for i in indent * 2 { out.push(' '); }
}

// Renders a node; `indent` < 0 means compact, no whitespace at all.
recursive fn render(n: Node&, out: u8[>..]&, indent: i64) {
    match n.val {
        Null => out.append("null"),
        Bool b => out.append(if b.v { "true" } else { "false" }),
        Num x => format(out, x.v),
        Str &s => quote(out, s.s),
        Arr &a => render_children(a.first, false, out, indent, '[', ']'),
        Obj &o => render_children(o.first, true, out, indent, '{', '}'),
    }
}

recursive fn render_children(first: Node?, keyed: bool, out: u8[>..]&, indent: i64, open: u8, close: u8) {
    out.push(open);
    var c = first;
    var i = 0;
    while c {                            // a plain reference inside the loop
        if i > 0 { out.push(','); }
        if indent >= 0 { newline(out, indent + 1); }
        if keyed { quote(out, c.key); out.push(':'); if indent >= 0 { out.push(' '); } }
        render(c, out, if indent >= 0 { indent + 1 } else { -1 });
        c .= c.next;
        i++;
    }
    if i > 0 && indent >= 0 { newline(out, indent); }
    out.push(close);
}

// --- querying ------------------------------------------------------------

// The member of an object named `key`, or null.
fn member(n: Node&, key: u8[:]) -> Node? {
    match n.val {
        Obj &o => {
            var c = o.first;
            while c {
                if c.key == key { return c; }
                c .= c.next;
            }
            null
        },
        _ => null,
    }
}

// Element i of an array, or null.
fn element(n: Node&, i: i64) -> Node? {
    match n.val {
        Arr &a => {
            var c = a.first;
            var k = 0;
            while c {
                if k == i { return c; }
                c .= c.next;
                k++;
            }
            null
        },
        _ => null,
    }
}

fn number_of(n: Node?) -> f64 {
    guard n else { return 0.0; }
    match n.val { Num x => x.v, _ => 0.0 }
}

fn main() {
    var path: u8[>..] = [];
    if arg_count() > 1 { arg(1, path); } else { path.append("data/sample.json"); }
    var src: u8[>..] = [];
    guard read_file(path, src) else { print("cannot read ", path); exit(1); }

    let root, err = parse(src);
    guard root else { print("parse error: ", err); exit(1); }
    print("parsed ", src.len, " bytes into ", pool.len, " nodes");

    var out: u8[>..] = [];
    render(root, out, 0);
    print(out);
    out.clear();
    render(root, out, -1);
    print("compact: ", out.len, " bytes");
    print(out);

    // Walking the tree by key and index.
    let name = member(root, "name");
    if name { var s: u8[>..] = []; render(name, s, -1); print("name = ", s); }
    let nested = member(root, "nested");
    if nested {
        let depth = member(nested, "depth");
        print("nested.depth = ", number_of(depth));
        let list = member(nested, "list");
        if list { print("nested.list[2] = ", number_of(element(list, 2))); }
    }
    let authors = member(root, "authors");
    if authors {
        var i = 0;
        loop {
            let a = element(authors, i);
            guard a;
            let born = number_of(member(a, "born"));
            var s: u8[>..] = [];
            let nm = member(a, "name");
            if nm { render(nm, s, -1); }
            print("author ", i, ": ", s, " born ", born);
            i++;
        }
    }

    // Errors land at `parse` with a position, from wherever they happened.
    for bad in ["[1, 2", "{\"a\" 1}", "[tru]", "\"abc", "{\"k\": [1, 2,]}", "1 2"] {
        let r, e = parse(bad);
        print("'", bad, "' -> ", e);
    }
}
