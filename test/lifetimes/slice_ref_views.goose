// A load through a reference-to-slice parameter whose argument names a
// slice variable the callee cannot name, or a temporary's slot, sees the
// slice that slot held as the call was made, a class of its own: what the
// callee returns or stores of it points where that slice did, and whether
// an array the callee shrinks is the one that slice views is its callers' to
// judge (§3.8, §9.2). A store into the slot while the callee runs joins it.
struct Word { text: u8[:] }
struct Pt { x: f64 }
struct Bag { xs: i64[4], items: Pt[>..<] }
struct Box { s: u8[:] }
var src: u8[>..] = [];
var other: u8[>..] = [];

fn take(p: u8[:]&, n: i64) -> u8[:] {
    let r = p[0..n];
    p = p[n..];
    return r;
}

// A cursor over the text, returned past the function that owns the cursor.
fn first_word(text: u8[:]) -> u8[:] {
    var cur = text;
    return take(cur, 5);
}

// The same through a reference passed on to another cursor helper.
fn skip_take(p: u8[:]&) -> u8[:] {
    let skipped = take(p, 6);
    return take(p, 5);
}
fn second_word(text: u8[:]) -> u8[:] {
    var cur = text;
    return skip_take(cur);
}

// Tokens pushed into the caller's array point where the cursor's slice does.
fn next(cur: u8[:]&, out: (u8[:])[>..]&) {
    out.push(cur[0..1]);
    cur = cur[1..];
}

// What a store into the slot replaces is still the caller's slice.
fn swap_out(p: u8[:]&) -> u8[:] {
    let old = p[0..2];
    p = other[..];
    return old;
}

fn load(p: u8[:]&) -> u8[:] { return p; }

// A slice of the one the slot holds, kept in the caller's holder, views
// what that slice does, not whatever the slot outlives.
fn put(p: u8[:]&, b: Box&) { b.s = p[0..1]; }

// A reference into the slice may be kept where its storage holds no
// grow-shrink array that can hold the element.
fn keep_first(p: i64[:]&, kept: (i64&)[>..]&) { kept.push(p[0]); }
fn mk() -> Word { return Word { text: src[6..11] }; }

// Whether gs is the array the slice behind pp views only the call sites
// know; this one passes an unrelated array.
fn peek(pp: i64[:]&, gs: i64[>..<]&) -> i64 {
    gs.clear();
    gs.push(5);
    return pp[0];
}

fn main() {
    src.append("hello world");
    other.append("xy");
    print(first_word(src[..]), "|", second_word(src[..]));
    var toks: (u8[:])[>..] = [];
    var cur = src[0..3];
    for i in 3 { next(cur, toks); }
    print(toks.len, " ", toks[0], toks[1], toks[2], " ", cur.len);
    var s = src[..];
    let old = swap_out(s);
    other.clear();
    print(old);
    let x = load(mk().text);
    print(x);
    var a: i64[>..<] = [7];
    var b: i64[>..<] = [];
    var t = a[..];
    print(peek(t, b), " ", b[0]);
    var bag = Bag { xs: [5, 6, 7, 8], items: [Pt { x: 1.0 }] };
    var kept: (i64&)[>..] = [];
    var xs = bag.xs[1..];
    keep_first(xs, kept);
    print(kept[0], " ", bag.items.len);
    var w: u8[>..] = [];
    w.append("pq");
    var z: u8[>..] = [];
    z.append("z");
    var u = w[..];
    var bx = Box { s: w[0..0] };
    put(u, bx);
    z.clear();
    print(bx.s, " ", z.len);
}
