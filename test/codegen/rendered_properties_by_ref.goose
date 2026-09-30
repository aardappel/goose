// runtime-debug: a user format overload taking an i64 by reference, given
// a property (.len, .cap) print, str or format renders, refers to a
// temporary holding the value read just before it runs (§3.7): growing the
// array whose length it was given leaves what the reference reads as it
// was, and an overload after it reads the new length. The overload records
// every value it renders, and would mark one its reference no longer reads.
var seen: i64[>..] = [];
fn format(out: u8[>..]&, v: i64&) {
    let before = v;
    seen.push(before);
    out.append("<");
    format(out, v as u64);
    if v != before { out.append("!"); }
    out.append(">");
}

struct H { buf: i64[..4], name: u8[] }
var h = H { buf: [], name: "three" };
var rows: i64[..4][>..] = [];
fn get_seen() -> i64[>..]& { return seen; }
fn seen_len() -> i64 { return seen.len; }

fn through_ref(r: i64[>..]&, s: i64[:]) {
    print("ref ", r.len, " ", r.len, " slice ", s.len);
}

fn main() {
    print("seen ", seen.len, " ", seen.len);
    h.buf.push(7);
    print("buf ", h.buf.cap, " ", h.buf.len, " name ", h.name.len);
    rows.push([1, 2, 3]);
    print("row ", rows[0].len, " of ", rows[0].cap);
    through_ref(seen, seen[1..]);
    // Calls whose inlined bodies are properties, and a construct the
    // optimizer folds to one.
    let pick = true;
    print("calls ", get_seen().len, " ", seen_len(), " ", if pick { seen.len } else { 0 });
    // str, and format into a resizable and into a limited array.
    let s = str(h.buf.cap, "/", seen.len);
    print("str ", s);
    var o: u8[>..] = [];
    format(o, seen.len, seen.len);
    var l: u8[..32] = [];
    format(l, h.buf.cap, seen.len);
    print("format ", o, " ", l);
    print("rendered ", seen.len);
}
