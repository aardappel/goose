// runtime-debug: a property (.len, .cap) that print, str or format renders
// with a user format overload for i64 is a value, not a path into its
// array (§3.7): it is read just before the overload rendering it runs,
// after the overloads of the arguments before it, which may have grown the
// array. The overload records every value it renders, so each line shows
// when its properties were read.
var seen: i64[>..] = [];
fn format(out: u8[>..]&, v: i64) {
    seen.push(v);
    out.append("<");
    format(out, v as u64);
    out.append(">");
}

struct H { buf: i64[..4], name: u8[] }
var h = H { buf: [], name: "three" };
var rows: i64[..4][>..] = [];
fn get_seen() -> i64[>..]& { return seen; }
fn buf_len() -> i64 { return h.buf.len; }

fn through_ref(r: i64[>..]&, s: i64[:]) {
    print("ref ", r.len, " ", r.len, " slice ", s.len);
}

fn main() {
    // A global resizable array, twice in one print: the second is read
    // after the first's overload pushed onto it.
    print("seen ", seen.len, " ", seen.len);
    // A limited array's capacity and length in a field, a string's length.
    h.buf.push(7);
    print("buf ", h.buf.cap, " ", h.buf.len, " name ", h.name.len);
    // An element's length, a reference's and a slice's.
    rows.push([1, 2, 3]);
    print("row ", rows[0].len, " of ", rows[0].cap);
    through_ref(seen, seen[1..]);
    // Calls whose inlined bodies are properties, and a construct the
    // optimizer folds to one.
    let pick = true;
    print("calls ", get_seen().len, " ", buf_len(), " ", if pick { h.buf.cap } else { 0 });
    // str, and format into a resizable and into a limited array.
    let s = str(h.buf.cap, "/", seen.len);
    print("str ", s);
    var o: u8[>..] = [];
    format(o, h.buf.len, seen.len);
    var l: u8[..32] = [];
    format(l, h.buf.cap, seen.len);
    print("format ", o, " ", l);
    print("rendered ", seen.len);
}
