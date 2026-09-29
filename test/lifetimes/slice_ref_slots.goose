// A reference to a slice names the slot holding it -- a slice variable, a
// field, an element -- whether it binds the slot by reference or is taken
// with an explicit `&` (§3.8, §4.1): a load through it sees what the slot
// holds then, a write through it assigns the slot, and it lives as long as
// the slot does.
struct E { text: u8[:] }
struct Named { rs: u8[:]& }
var src: u8[>..] = [];
var gview: u8[:] = src[..];

fn take(p: u8[:]&, n: i64) -> u8[:] {
    let r = p[0..n];
    p = p[n..];
    return r;
}

fn load(p: u8[:]&) -> u8[:] { return p; }
fn pass(p: u8[:]&) -> u8[:] { return load(p); }

// A global slice variable's slot outlives every caller.
fn global_view() -> u8[:]& { return gview; }

fn main() {
    src.append("hello world");
    // A cursor through a local slice variable, spelled either way.
    var cur = src[..];
    let a = take(cur, 5);
    let b = take(&cur, 1);
    print(a, "|", b, "|", cur);
    // Element and field slots, and a reference passed on.
    var views: (u8[:])[>..] = [];
    views.push(src[0..5]);
    views.push(src[6..11]);
    print(views.index_of(views[1]), " ", load(views[0]), " ", pass(views[1]));
    let n = Named { rs: views[0] };
    print(n.rs);
    var entries: E[>..] = [];
    entries.push(E { text: src[6..8] });
    print(load(entries[0].text), " ", load(&entries[0].text).len);
    // A write through the reference assigns the variable it names.
    let r: u8[:]& = cur;
    r = src[0..4];
    print(cur);
    gview = src[..];
    let g = global_view();
    print(g.len);
}
