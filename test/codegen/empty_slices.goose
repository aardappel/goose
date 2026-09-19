// Empty slices whose data pointer is null -- the zero value default<T>() and
// a default element hold -- print, build text, append, compare, construct
// arrays and serialize the way empty views into storage do.
import os;

struct Named { name: const u8[:] }
struct Ints { xs: const i32[:] }
struct Holder { v: u8[] }
enum Msg { Text { s: const u8[:] }, Num { v: i64 } }

// Rendered through the optional reference, not as the slice a plain one
// decays to.
fn show(s: const u8[:]&?) { print("[", s, "]"); }
fn image_len(s: const i32[:]&) -> i64 { to_bytes(s).len }

fn main() {
    let d = default<Named>();
    let di = default<Ints>();
    let text = "abc";
    let view = text[1..1];               // Empty, into a literal.
    var nums: i32[>..] = [4, 5];
    let iview = nums[2..];               // Empty, into an array.

    // Text: print, str, and format into a resizable and a limited array.
    print("[", d.name, "][", view, "]");                  // [][]
    print(d, " ", di);                                    // Named { "" } Ints { [] }
    print(default<Named[2]>());                           // [Named { "" }, Named { "" }]
    show(d.name);                                         // []
    let s = str("<", d.name, view, ">");
    print(s, " ", s.len);                                 // <> 2
    var out: u8[>..] = "(";
    format(out, d.name, view, ")");
    print(out);                                           // ()
    var lim: u8[..4] = [];
    format(&lim, d.name, view);
    print(lim.len);                                       // 0

    // Appends to a resizable and a limited array.
    var acc: u8[>..] = "x";
    acc.append(d.name);
    acc.append(view);
    var ai: i32[>..] = [1];
    ai.append(di.xs);
    ai.append(iview);
    var li: i32[..2] = [];
    li.append(di.xs);
    li.append(iview);
    print(acc, " ", ai.len, " ", li.len);                 // x 1 0

    // Comparisons.
    print(d.name == "", " ", d.name == view, " ", di.xs == iview, " ", d.name == "a");
                                                          // true true true false

    // Arrays built from them.
    var grown: u8[>..] = d.name;
    let fixed: u8[..8] = d.name;
    let h = Holder { d.name };
    print(grown.len, " ", fixed.len, " ", h == Holder { "" });   // 0 0 true

    // Serialized: a zero count and nothing after it.
    let img = to_bytes(di.xs);
    var into: u8[>..] = [];
    to_bytes(di.xs, into);
    var intolim: u8[..4] = [];
    to_bytes(di.xs, &intolim);
    print(img.len, " ", into.len, " ", intolim.len, " ", image_len(di.xs));   // 1 1 1 1

    // A path the OS layer copies into a C string.
    print(file_exists(d.name));                           // false

    // A default element and a default payload.
    reusable[] var pool: Named[>..] = [];
    let ps = pool.alloc_slice(2);
    match default<Msg>() {
        Text t => print("[", ps[1].name, t.s, "]"),      // []
        Num x => print(x.v)
    }
}
