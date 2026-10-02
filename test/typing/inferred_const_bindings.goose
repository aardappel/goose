// An un-annotated variable's type is its initializer's, `const` where that
// value is read-only (§9.5), even where the value's type does not say so: a
// `u8[:]` result or parameter read-only in this instantiation. So assigning
// it another read-only slice fits, as it would for `var t: const u8[:]`; in
// a writable instantiation the same declaration writes through.
import std;

fn trim_twice(line: const u8[:]) -> i64 {
    var t = trim(line);
    let n = 2;
    t = trim(t[..n]);
    return t.len;
}

// Read-only for a literal, writable for a buffer, whose first byte it bumps.
fn bump_tail(s: u8[:]) -> u8 {
    var t = s;
    t = s[1..];
    if t.len > 1 && t[0] == 'x' { return t[1]; }
    return t[0];
}

fn bump_buf(s: u8[:]) -> u8 {
    var t = s;
    t = s[1..];
    t[0] += 1;
    return t[0];
}

fn halves(s: u8[:]) -> u8[:], u8[:] { return s[..1], s[1..]; }

fn split_halves(line: const u8[:]) {
    var a, b = halves(line);
    a = b;
    b = line;
    print(a, " ", b);
}

fn pick(xs: i64[>..]&) -> i64[>..]& { return xs; }

// A reference to a non-fixed value binds as one (§4.1), read-only here.
fn rebind(xs: i64[>..]&, ys: i64[>..]&) -> i64 {
    var r = pick(xs);
    r .= ys;
    return r[0];
}

var gtrim = trim("  global  ");

fn main_inferred_const_bindings() {
    print(trim_twice("  hello  "), " ", trim_twice("a b"));
    var buf: u8[] = "axyz";
    print(bump_tail("abc"), " ", bump_tail(buf[..]), " ", bump_buf(buf[..]), " ", buf);
    split_halves("pq");
    let lit = trim(" lit ");
    var t = lit;
    t = " other "[1..6];
    print(t, " ", lit);
    let cx: const i64[>..] = [1, 2];
    let cy: const i64[>..] = [3];
    var mx: i64[>..] = [5];
    print(rebind(cx, cy), " ", rebind(mx, mx));
    gtrim = gtrim[1..];
    print(gtrim);
}

fn main() { main_inferred_const_bindings(); }
