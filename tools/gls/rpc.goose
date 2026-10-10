import json;

fn json_member(n: json::Node?, key: const u8[:]) -> json::Node? { json::member(n, key) }

fn json_is_object(n: json::Node?) -> bool {
    guard n else { return false; }
    match n.val {
        Obj => { return true; },
        _ => { return false; },
    }
}

fn json_is_array(n: json::Node?) -> bool {
    guard n else { return false; }
    match n.val {
        Arr => { return true; },
        _ => { return false; },
    }
}

// The text of a string node, or empty for anything else.
fn json_string(n: json::Node?) -> const u8[:] {
    let s, ok = json::as_string(n);
    return s;
}

fn json_integer(n: json::Node?, fallback: i64) -> i64 {
    let v, ok = json::as_i64(n);
    return if ok { v } else { fallback };
}

fn json_bool(n: json::Node?, fallback: bool) -> bool {
    let v, ok = json::as_bool(n);
    return if ok { v } else { fallback };
}

// A request id, written back as the JSON it was received as.
fn json_raw_id(n: json::Node?, out: u8[>..]&) {
    guard n else { out.append("null"); return; }
    match n.val {
        Int i => { json::format_int(out, i.v); },
        Num x => { json::format_number(out, x.v); },
        Str s => { json::format_string(out, s.s); },
        _ => { out.append("null"); },
    }
}

fn json_quote(out: u8[>..]&, s: const u8[:]) { json::format_string(out, s); }
