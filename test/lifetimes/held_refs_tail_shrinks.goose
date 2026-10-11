// A struct's grow-only tail shrinks through a reference to the struct while
// the struct is still used, where nothing it holds can refer into the tail
// (§5.1): its references are paths to whole arrays or tables, point at
// storage of a type the tail's elements cannot hold, or were stored with
// views of other arrays only.
import std;
import dictionary;

let NAMES = ["John", "Jane"];

// A reference to another whole array.
struct Echo { text: u8[>..]&, out: u8[>..] }

fn echo(e: Echo&) {
    e.out.clear();
    e.out.append(e.text);
}

// The tail shrunk through a reference field, beside a tail of the same type.
struct Render { n: i64, rendered: u8[>..]&, text: u8[>..] }

fn render(b: Render&) {
    var r: u8[>..] = [];
    r.append(b.text);
    b.rendered.clear();
    b.rendered.append(r);
    b.n += 1;
}

// A reference to a table of inline arrays, and one to a table of slices of
// static data.
type Fixed = dictionary<u8[..20], u8[..16]>;
type Views = dictionary<const u8[:], const u8[:]>;
struct Template { fixed: Fixed&, views: Views&, text: u8[>..]&, out: u8[>..] }

fn fill(t: Template&) {
    var r: u8[>..] = [];
    r.append(t.text);
    let f = t.fixed.get("k1");
    if f { r.append(f); }
    let v = t.views.get("k2");
    if v { r.append(v); }
    t.out = copy(r);
    t.out.push('.');
}

// A reference to storage of a type the tail's elements are not.
struct Cell { x: i32, y: i32 }
struct Pos { y: i32, x: i32 }
struct Walk { start: Cell&, steps: u32, path: Pos[>..] }

fn route(w: Walk&) -> Pos[>..] {
    var p: Pos[>..] = [];
    for i in 3 { p.push(Pos { y: w.start.y + i as i32, x: w.start.x }); }
    return p;
}

fn walk(w: Walk&) {
    w.path.clear();
    w.path.append(w.route());
    w.steps += w.path.len as! u32;
}

fn rewalk(w: Walk&) {
    let p = w.route();
    w.path = copy(p);
    w.steps += w.path.len as! u32;
}

// A reference of the element type, pointing elsewhere.
struct Track { last: Pos&, path: Pos[>..] }

fn retrack(t: Track&) {
    t.path = [Pos { y: 1, x: 2 }];
    t.last.x += t.path[0].x;
}

// Arrays of references and of slices, refilled through a reference.
fn refill(dst: (i64&)[>..]&, r: i64&) {
    dst.clear();
    dst.push(r);
}

struct Lists { n: i32, lists: (i32[:])[>..] }

fn relist(g: Lists&, xs: i32[:]) {
    g.lists.clear();
    g.lists.push(xs);
    g.n += 1;
}

// A view of another array, made by a call and stored into a table the
// struct refers to.
struct Slot { s: const u8[:] }
struct Keyed { keys: u8[>..]&, slot: Slot&, out: u8[>..] }

fn key(keys: u8[>..]&, i: i64) -> const u8[:] {
    let start = keys.len;
    keys.push('k');
    keys.push(i as u8 + '0');
    return keys[start..];
}

fn rekey(k: Keyed&) {
    k.out.clear();
    k.out.append(k.slot.s);
}

fn put(slot: Slot&, s: const u8[:]) { slot.s = s; }

fn keyed(k: Keyed&) {
    k.slot.s = key(k.keys, 1);
    for i in 2 { rekey(k); }
}

fn keyed_by_call(k: Keyed&) {
    put(k.slot, key(k.keys, 2));
    for i in 2 { rekey(k); }
}

fn main() {
    var text: u8[>..] = [];
    text.append("abc");
    var e = Echo { text: text, out: [] };
    for i in 2 { echo(e); }
    print(e.out);

    var rendered: u8[>..] = [];
    var b = Render { n: 0, rendered: rendered, text: [] };
    b.text.append("xyz");
    for i in 3 { render(b); }
    print(rendered, " ", b.n);

    var fixed = Fixed {};
    fixed.insert(str("k", 1), NAMES[1]);
    var views = Views {};
    views.insert("k2", NAMES[0]);
    var t = Template { fixed: fixed, views: views, text: text, out: [] };
    for i in 2 { fill(t); }
    print(t.out);

    var c = Cell { x: 1, y: 2 };
    var w = Walk { start: c, steps: 0, path: [] };
    for i in 2 { walk(w); }
    rewalk(w);
    print(w.steps, " ", w.path.len);

    var last = Pos { y: 0, x: 0 };
    var tr = Track { last: last, path: [] };
    for i in 2 { retrack(tr); }
    print(last.x);

    var x = 7;
    var a: (i64&)[>..] = [];
    for i in 2 { refill(a, x); }
    print(a.len, " ", a[0]);

    var nums: i32[>..] = [1, 2, 3];
    var g = Lists { n: 0, lists: [] };
    for i in 2 { relist(g, nums[1..]); }
    print(g.n, " ", g.lists[0]);

    var keys: u8[>..] = [];
    var slot = Slot { s: "" };
    var k = Keyed { keys: keys, slot: slot, out: [] };
    keyed(k);
    print(k.out);
    keyed_by_call(k);
    print(k.out);
}
