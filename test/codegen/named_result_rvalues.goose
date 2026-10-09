// A named result stays at its destination when the other returns hand back
// values built without it: each such value is built behind the local's
// elements and moved down over them (§7.3). Returns before the local is
// declared, from inside a loop and from an inlined body; resizable and
// variable-array results; a return reading the local, which keeps the local
// off the destination.
fn evens(n: i64) -> i64[>..] {
    if n < 0 { return []; }
    var r: i64[>..] = [];
    for i in n {
        if i == 7 { return [70, 71]; }
        r.push(2 * i);
    }
    return r;
}
fn text(n: i64) -> u8[] {
    var s: u8[>..] = [];
    s.append("ab");
    if n == 1 { return str("one ", n); }
    if n == 2 { return "two"; }
    s.push('c');
    return s;
}
// Small enough to inline at every call below.
fn pair(a: i64) -> i64[>..] {
    var r: i64[>..] = [a];
    if a == 0 { return []; }
    r.push(a + 1);
    return r;
}
fn reads(n: i64) -> u8[>..] {
    var s: u8[>..] = [];
    s.append("xyz");
    if n > 0 { return s[0..n]; }
    return s;
}
fn main() {
    print(evens(-1), " ", evens(3), " ", evens(9));
    print(text(0), " ", text(1), " ", text(2));
    var all: i64[>..] = [];
    all.append(pair(5));
    all.append(pair(0));
    all.append(pair(8));
    let p = pair(3);
    let q = pair(0);
    print(all, " ", p, " ", q);
    print(reads(0), " ", reads(2));
    // The values land where a later push expects the array to end.
    var e = evens(9);
    e.push(99);
    var t = evens(2);
    t.push(98);
    print(e, " ", t);
}
