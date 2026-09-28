// An array, or a reference to one, meeting a destination of a slice type of
// its element type is a whole-array slice of it (§3.10), wherever the
// destination is: an annotated local or global, an assignment to a slice
// variable, a field or an element, a struct or variant literal's field, a
// field default, an array literal's element, a return, a push. The slice
// views the array itself, const where the array is read-only, and a
// generic struct literal infers its element type through it.
struct Item { name: u8[..16], count: i64 }
struct H { s: const u8[:], n: i64 }
struct V<T> { s: T[:] }
struct D { s: const u8[:] = GNAME }
struct Doc { title: u8[..8], body: u8[>..] }
struct Rec { tag: u8[..4], data: u8[] }
enum Tagged { Plain, Named { s: const u8[:] } }

const TABLE: u8[3] = [120, 121, 122];
var GNAME: u8[..8] = "global";
var GNUMS: i64[3] = [7, 8, 9];
var GV: const u8[:] = GNAME;
let GL: const i64[:] = GNUMS;

fn view(r: u8[..16]&) -> const u8[:] { return r; }
fn gview() -> const i64[:] { GNUMS }
fn named() -> const u8[:], i64 { return GNAME, GNAME.len; }
fn get_ref() -> u8[..8]& { GNAME }
fn first(xs: i64[:]) -> i64 { xs[0] }

fn main() {
    var item = Item { name: "sword", count: 1 };
    // Annotated bindings of every array kind, and through a reference.
    let a: const u8[:] = item.name;
    var nums: i64[3] = [1, 2, 3];
    var w: i64[:] = nums;
    w[0] = 10;
    var buf: u8[>..] = [];
    format(buf, "gold");
    let b: const u8[:] = buf;
    let c: const u8[:] = get_ref();
    let t: const u8[:] = TABLE;
    print(a, " ", nums[0], " ", b, " ", c, " ", t, " ", GV, " ", GL[2]);
                                                  // sword 10 gold global xyz global 9
    // The slice views the array: writes to it show, growth does not.
    buf.push(33);
    item.name[0] = 'S';
    print(a, " ", b.len, " ", buf.len);           // Sword 4 5

    // Assignments to a slice variable, a field and an element.
    var more: i64[2] = [4, 5];
    w = more;
    var h = H { s: item.name, n: 1 };
    h.s = GNAME;
    var parts: const u8[:][2] = [item.name, TABLE];
    parts[1] = buf;
    print(w.len, " ", first(w), " ", h.s, " ", parts[0], " ", parts[1]);
                                                  // 2 4 global Sword gold!

    // A variant's payload, a field default, a generic struct literal.
    let d = D {};
    let tg: Tagged = Tagged.Named { s: item.name };
    let g = V { s: nums };
    match tg { Named p => print(p.s, " ", d.s, " ", g.s[0]), Plain => print("plain") }
                                                  // Sword global 10

    // Returns, a multi-value return, and pushes into an array of slices.
    var names: const u8[:][>..] = [];
    names.push(item.name);
    names.push(view(item.name));
    let nm, len = named();
    print(view(item.name), " ", gview()[1], " ", names.len, " ", names[1], " ", nm, len);
                                                  // Sword 8 2 Sword global6

    // Arrays of variable-size elements, a frame object's resizable tail and
    // a variable-size field, iterated or printed through their slices.
    let ws: u8[][] = ["a", "bb", "ccc"];
    let s: const u8[][:] = ws;
    var n = 0;
    for x in s { n += x.len; }
    var doc = Doc { title: "t", body: [] };
    format(doc.body, "body text");
    let body: const u8[:] = doc.body;
    let r = Rec { tag: "x", data: "payload" };
    let data: const u8[:] = r.data;
    print(n, " ", body, " ", data);               // 6 body text payload

    // Indexing and loops over a slice bound from an array.
    var arr: i64[5] = [1, 2, 3, 4, 5];
    var v: i64[:] = arr;
    for i in v.len { v[i] = v[i] * 2; }
    var sum = 0;
    for i in 0..v.len { sum += v[i]; }
    print(sum, " ", arr[4], " ", v[v.len - 1], " ", a == item.name, " ", b == "gold!");
                                                  // 30 10 10 true false

    // A slice variable first bound to static data retargets to a global.
    var label = "literal";
    label = GNAME;
    print(label);                                 // global
}
