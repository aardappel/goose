// runtime-debug: stdlib/json.goose. The writer's strings, escapes and
// numbers; the document tree of every kind of value, read back out with
// the accessors and written out again; escapes decoded, surrogate pairs
// included; integers that do and do not fit an i64; numbers that take the
// exact rounding (halfway cases, the subnormals, the edges of the range),
// through the tree and the reader alike; random doubles written and read
// back to the bit; deep nesting; every kind of malformed text, with the
// error it gives; and the reader mapping objects onto a struct, passing over
// what it does not know, with its errors.
import std;
import json;

extern fn ldexp(x: f64, e: i32) -> f64;

// The tree as compact JSON. Nodes are in text order, so one pass over them
// writes it, with a count of the children each open container has left.
fn serialize(out: u8[>..]&, doc: json::Node[>..]&) {
    var left: i64[>..<] = [];
    var obj: bool[>..<] = [];
    var first = false;              // whether the next node is its container's first
    for &n in doc {
        if left.len > 0 {
            if !first { out.push(','); }
            if obj[obj.len - 1] { json::format_key(out, n.key); }
            left[left.len - 1] -= 1;
        }
        first = false;
        match n.val {
            Null => { out.append("null"); },
            Bool b => { json::format_bool(out, b.v); },
            Int i => { json::format_int(out, i.v); },
            Num x => { json::format_number(out, x.v); },
            Str s => { json::format_string(out, s.s); },
            Arr a => { out.push('['); left.push(a.count); obj.push(false); first = true; },
            Obj o => { out.push('{'); left.push(o.count); obj.push(true); first = true; },
        }
        while left.len > 0 && left[left.len - 1] == 0 {
            out.push(if obj[obj.len - 1] { '}' } else { ']' });
            left.pop();
            obj.pop();
            first = false;
        }
    }
}

// text parsed and written out again, or the error.
fn reparse(text: u8[:]) -> u8[>..] {
    var doc: json::Node[>..] = [];
    var decoded: u8[>..] = [];
    let root, err = json::parse(text, doc, decoded);
    guard root else { return err; }
    var out: u8[>..] = [];
    serialize(out, doc);
    return out;
}

// s with DEL and the bytes above ASCII written as \xNN, for printing.
fn ascii(s: u8[:]) -> u8[>..] {
    var out: u8[>..] = [];
    for c in s {
        if c < 0x7f { out.push(c); continue; }
        out.append("\\x");
        format_uint(out, c, 16, 2, '0');
    }
    return out;
}

fn number(v: f64) -> u8[>..] {
    var out: u8[>..] = [];
    json::format_number(out, v);
    return out;
}

// The number in text, through the tree and through the reader, which must
// agree to the bit.
fn parsed(text: u8[:]) -> f64 {
    var doc: json::Node[>..] = [];
    var decoded: u8[>..] = [];
    let root, err = json::parse(text, doc, decoded);
    guard root else { abort(err); }
    let v, ok = json::as_f64(root);
    assert(ok);
    var r = json::reader(text);
    let w = json::read_number(r);
    if !json::finish(r) { abort(json::message(r)); }
    if !(v == w && 1.0 / v == 1.0 / w) { abort(str("tree and reader differ on ", text)); }
    return v;
}

struct Item { id: i64, name: u8[..16], price: f64, tags: i64, ok: bool }

fn read_items(text: u8[:], items: Item[>..]&) -> u8[>..] {
    var r = json::reader(text);
    var scratch: u8[>..] = [];
    json::each_member(r) { key =>
        if key == "items" {
            json::each_element(r) {
                var it = Item { id: 0, name: [], price: 0.0, tags: 0, ok: false };
                json::each_member(r) { k =>
                    if k == "id" { it.id = json::read_int(r); }
                    else if k == "name" { it.name.append(json::read_string(r, scratch)); }
                    else if k == "price" { it.price = json::read_number(r); }
                    else if k == "ok" { it.ok = json::read_bool(r); }
                    else if k == "tags" { json::each_element(r) { json::skip(r); it.tags++; }; }
                    else { json::skip(r); }
                };
                items.push(it);
            };
        } else {
            json::skip(r);
        }
    };
    if !json::finish(r) { return json::message(r); }
    return str("ok");
}

fn main() {
    // Writing.
    var out: u8[>..] = [];
    json::format_string(out, "plain");
    out.push(' ');
    json::format_string(out, "q\"b\\s/ \n\r\t \x01\x1f\x7f caf\xc3\xa9");
    out.push(' ');
    json::format_key(out, "k\"ey");
    json::format_int(out, -9223372036854775807 - 1);
    out.push(' ');
    json::format_int(out, 9223372036854775807);
    out.push(' ');
    json::format_bool(out, true);
    out.push(' ');
    json::format_bool(out, false);
    print(ascii(out));
    let values = [0.0, -0.0, 1.0, -2.0, 0.5, 123.25, 0.1, 1.0 / 3.0, 2.0 / 3.0, 100.0, 1e-7,
                  1.5e300, 0.001, 0.0001, 1e-5, 1e-6, 1e15, 1e16, 123456789012345680.0, 1e21,
                  1e22, 1e23, 9007199254740992.0, 9007199254740993.0, 4.35, 5e-324,
                  2.2250738585072014e-308, 1.7976931348623157e308, -1.7976931348623157e308,
                  0.30000000000000004, 1.0 / 0.0, -1.0 / 0.0, 0.0 / 0.0];
    out.clear();
    for v, i in values {
        if i > 0 { out.push(' '); }
        json::format_number(out, v);
    }
    print(out);

    // The document tree.
    let doc_text = """
         { "a": [1, -2, 3.5, -0.0, 1e2, true, false, null, "s", {}, []],
          "nested": {"x": {"y": {"z": "deep"}}}, "esc": "\u00e9\ud83d\ude00\"\\\/\b\f\n\r\t",
          "lone": "\ud800!", "k\u0065y": 7, "big": 9223372036854775807,
          "small": -9223372036854775808, "over": 9223372036854775808,
          "long": 12345678901234567890, "e": 1E-2, "dup": 1, "dup": 2 }
        """;
    var doc: json::Node[>..] = [];
    var decoded: u8[>..] = [];
    let root, err = json::parse(doc_text, doc, decoded);
    guard root else { abort(err); }
    out.clear();
    serialize(out, doc);
    print(ascii(out));
    print(doc.len, " nodes, ", decoded.len, " bytes decoded");
    let z, zok = json::as_string(json::member(json::member(json::member(json::member(root, "nested"), "x"), "y"), "z"));
    let missing = json::member(json::member(root, "nested"), "nope");
    let chained = json::member(json::member(root, "missing"), "x");
    let key, kok = json::as_i64(json::member(root, "key"));
    let dup, dok = json::as_i64(json::member(root, "dup"));
    let big, bok = json::as_i64(json::member(root, "big"));
    let small, sok = json::as_i64(json::member(root, "small"));
    let over, ook = json::as_i64(json::member(root, "over"));
    let overf, oook = json::as_f64(json::member(root, "over"));
    print(z, " ", zok, " ", missing == null, " ", chained == null, " ", key, " ", kok, " ", dup, " ",
          dok, " ", big, " ", bok, " ", small, " ", sok, " ", over, " ", ook, " ", number(overf), " ", oook);
    let a = json::member(root, "a");
    guard a else { abort("no a"); }
    out.clear();
    json::each(a) { e =>
        let f, fok = json::as_f64(e);
        let i, iok = json::as_i64(e);
        let b, bok2 = json::as_bool(e);
        let s, stok = json::as_string(e);
        format(out, "[", fok, " ", iok, " ", bok2, " ", stok, "]");
    };
    print(out);
    let wrong, wok = json::as_f64(json::member(root, "esc"));
    let none, nok = json::as_bool(null);
    print(wrong, " ", wok, " ", none, " ", nok, " ", json::member(a, "x") == null);
    match root.val {
        Obj o => { print("root object of ", o.count, " members"); },
        _ => { print("root is no object"); },
    }

    // Scalars at the top, white space around them, and empty containers.
    for t in ["0", " -12 ", "\"s\"", "true", "null", "{}", "[ ]", "[[], {}, [[]]]", "\t\r\n7\n"] {
        print(reparse(t));
    }

    // Deep nesting takes no recursion.
    var deep: u8[>..] = [];
    for i in 20000 { deep.push('['); }
    deep.push('1');
    for i in 20000 { deep.push(']'); }
    let deep_out = reparse(deep);
    print(deep_out.len, " ", deep_out == deep);

    // Malformed text.
    let bad = ["", "  ", "[", "[1,", "[1,]", "[1 2]", "{\"a\"}", "{\"a\":}", "{,}", "{\"a\":1,}",
               "{\"a\" 1}", "{1:2}", "[\"a\": 1]", "tru", "nul", "nulll", "01", "1.", "1e", "-",
               ".5", "+1", "\"abc", "\"a\nb\"", "\"\\x\"", "\"\\u12G4\"", "\"\\u12\"", "[1]]",
               "{}x", "[1}", "{\"a\":[}", "]", "\xef\xbb\xbf{}"];
    for t in bad {
        var shown: u8[>..] = [];
        format_replaced(shown, t, "\n", "\\n");
        print(ascii(shown), " => ", reparse(t));
    }

    // Exact rounding: every result through the tree and the reader alike.
    let exact = ["0.1", "0.2", "0.3", "1e23", "8.98846567431158e307", "2.2250738585072011e-308",
                 "2.2250738585072012e-308", "2.2250738585072014e-308", "4.9406564584124654e-324",
                 "2.4703282292062327e-324", "2.4703282292062328e-324", "1e-324", "3e-324",
                 "1.7976931348623157e308", "1.7976931348623158e308", "1.7976931348623159e308",
                 "1e400", "-1e400", "1e-400", "9007199254740993.0", "9007199254740995.0",
                 "1.00000000000000011102230246251565404236316680908203125",
                 "1.00000000000000011102230246251565404236316680908203125000000001",
                 "1.00000000000000011102230246251565404236316680908203124999999999",
                 "123456789012345678901234567890", "0.000000000000000000000000000000000000001234",
                 "7.2057594037927933e16", "3.0540412816658963e-16", "1448997445238699.0",
                 "-1e-7", "100000000000000000000000.0", "0e10", "-0.0", "0.0e-999", "1E+2",
                 "10000000000000000000000000000000000000000000000000000000000000000000000e-70",
                 "0.00000000000000000000000000000000000000000000000000000000000000000000001e70",
                 "4.4501477170144023e-308", "4.4501477170144022e-308", "1.2345678901234567e-300",
                 "6.631236846766476e-316", "3.9999999999999999999999999999999999999999"];
    for t in exact { print(t, " => ", number(parsed(t))); }

    // Random doubles, the subnormals included, written and read back.
    var r = rng { 5 };
    var mismatches = 0;
    var text: u8[>..] = [];
    for k in 20000 {
        let m = (r.rand_u64() >> 11) as! i64;
        let e = r.rand_int(2046) - 1074;
        var v = ldexp(m, e as! i32);
        if v - v != 0.0 { continue; }
        if r.rand_int(2) == 0 { v = -v; }
        text.clear();
        json::format_number(text, v);
        let back = parsed(text);
        if !(back == v && 1.0 / back == 1.0 / v) { mismatches++; print("no round trip: ", text); }
    }
    print(mismatches, " random doubles that did not read back");

    // The reader.
    let items_text = """
        {"skip": {"a": [1, {"b": null}], "c": "\u0041"}, "items": [
          {"id": 1, "name": "one", "price": 9.5, "tags": ["x", "y"], "ok": true, "extra": [[]]},
          {"name": "tw\u006f", "id": -2, "price": 1e3, "ok": false, "tags": []},
          {}], "after": 1.5e-3 }
        """;
    var items: Item[>..] = [];
    print(read_items(items_text, items));
    for it in items { print(it.id, " ", it.name, " ", number(it.price), " ", it.tags, " ", it.ok); }
    let reader_bad = ["{\"items\": [{\"id\": 1.5}]}", "{\"items\": [{\"id\": 1e2}]}",
                      "{\"items\": [{\"ok\": 1}]}", "{\"items\": [{\"name\": 5}]}",
                      "{\"items\": [{\"price\": \"x\"}]}", "{\"items\": [{\"id\": 1}]} x",
                      "{\"items\": [{\"id\": 1},]}", "{\"items\": {}}", "[]",
                      "{\"skip\": [1, 2}", "{\"skip\": {\"a\" 1}}", "{\"skip\": tru}",
                      "{\"skip\": \"\\q\"}", "{\"items\": [{\"name\": \"\\u00\"}]}",
                      "{\"skip\": [1, }", "{\"items\": [{\"id\": 9223372036854775808}]}"];
    for t in reader_bad {
        items.clear();
        print(t, " => ", read_items(t, items));
    }
    var rd = json::reader(" 42 ");
    let n42 = json::read_int(rd);
    print(n42, " ", json::finish(rd), " ", json::peek(rd));
}
