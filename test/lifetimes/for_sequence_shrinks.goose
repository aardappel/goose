// A for walks its sequence in place: a slice, an element or a field is live
// for the whole body, so only a shrink of the array it lies in is rejected.
// The body may shrink any other array, grow the one the sequence lies in, and
// shrink an array it iterates itself, whose length the loop reads again on
// every iteration; iterating a copy, a temporary or a reference read out
// first leaves every array free (§5.1, §5.2, §6.5).
struct Holder { r: i64[>..<]& }
fn mk() -> i64[3] { [4, 5, 6] }
fn tail(c: f64[>..<]&) -> f64[:] { c[1..] }
fn drain(a: i64[>..<]&) {
    for x in a { a.pop(); print(x); }
}
fn main() {
    // A slice of one array while the body shrinks another, of either kind.
    var c: f64[>..<] = [1.0, 2.0, 3.0, 4.0];
    var d: f64[>..<] = [5.0, 6.0, 7.0];
    for x in c[..] {
        d.pop();
        d.push(x);
        print(x, " ", d.len, " ", d[2]);
    }
    var g: f64[>..] = [1.0, 2.0];
    var h: f64[>..] = [8.0, 9.0];
    for y in g[1..] {
        h.pop();
        h.push(y * 10.0);
        print(y, " ", h[1]);
    }
    // A slice the array grows under keeps its length.
    for x in tail(c) {
        c.push(x + 10.0);
        d.clear();
        print(x, " ", c.len);
    }
    // A slice of either of two arrays while a third shrinks.
    let k = c.len > 1;
    for x in if k { c[5..] } else { h[..] } {
        d.push(x);
        d.pop();
        g.clear();
        print(x);
    }

    // The array itself, directly, through a reference, through a parameter
    // and through a reference field.
    var gs: i64[>..<] = [1, 2, 3, 4];
    for x in gs { gs.pop(); print(x); }
    var go: i64[3][>..] = [[1, 2, 3], [4, 5, 6]];
    for row in go { go.clear(); print(row[0]); }
    print(go.len);
    gs.push(7);
    gs.push(8);
    gs.push(9);
    let r = gs;
    for x in r { gs.pop(); print(x); }
    gs.push(10);
    gs.push(11);
    drain(gs);
    let hd = Holder { r: gs };
    for x in hd.r { gs.pop(); print(x); }

    // An element while its array grows, or while another array shrinks.
    var arr: i64[3][>..<] = [[1, 2, 3]];
    var other: i64[>..<] = [0, 0, 0];
    for x in arr[0] { arr.push([x, x, x]); other.pop(); print(x); }
    print(arr.len, " ", other.len);

    // A copy of an element, and a temporary.
    let row = arr[3];
    for x in row { arr.clear(); print(x); }
    for x in mk() { arr.push([x, 0, 0]); arr.pop(); print(x); }
    print(arr.len);

    // What the loop reads out of storage once: a reference taken with `&`,
    // a slice made of a slice element, a copy, and a branch's value, even
    // where the optimizer folds it into a path.
    var refs: (i64[>..<]&)[>..<] = [];
    gs.push(12);
    gs.push(13);
    refs.push(gs);
    for x in &refs[0] { refs.clear(); print(x); }
    var holders: (Holder&)[>..<] = [];
    holders.push(hd);
    for x in &holders[0].r { holders.clear(); print(x); }
    var text: u8[>..] = [];
    text.append("hey");
    var parts: (u8[:])[>..<] = [];
    parts.push(text[1..3]);
    for ch in parts[0][..] { parts.clear(); print(ch); }
    print(parts.len);
    refs.push(gs);
    for x in copy(refs[0]) { refs.clear(); print(x); }
    var rows: i64[3][>..<] = [[1, 2, 3]];
    for x in if true { rows[0] } else { mk() } { rows.pop(); rows.push([7, 7, 7]); print(x); }
}
