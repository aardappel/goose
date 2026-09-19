// append's source reached through a reference: an explicit `&` to a slice
// field, a slice variable and arrays of every kind, reference parameters,
// and a call returning a reference -- into a grow-only array and into a
// runtime- and a static-capacity limited one; an empty slice field whose
// data is null among them.
struct Ints { xs: const i32[:] }

fn from_array(r: i32[]&, a: i32[>..]&) { a.append(r); }
fn from_slice(r: const i32[:]&, a: i32[>..]&) { a.append(r); }
fn from_grow(r: i32[>..]&, a: i32[..]&) { a.append(r); }
fn from_fixed(r: i32[2]&, a: i32[..8]&) { a.append(r); }
fn pick(r: const i32[:]&) -> const i32[:]& { return r; }

fn main() {
    var arr: i32[] = [1, 2];
    var n = Ints { arr[..] };
    let e = default<Ints>();
    var s: const i32[:] = arr[1..];
    var g: i32[>..] = [3, 4];
    var f: i32[2] = [5, 6];
    var lim: i32[..4] = [7];

    // Explicit references.
    var a: i32[>..] = [];
    a.append(&n.xs);
    a.append(&e.xs);
    a.append(&s);
    a.append(&arr);
    a.append(&g);
    a.append(&f);
    a.append(&lim);
    print(a);                                   // [1, 2, 2, 1, 2, 3, 4, 5, 6, 7]

    // Reference parameters and a reference-returning call.
    var b: i32[>..] = [];
    from_array(arr, b);
    from_slice(n.xs, b);
    from_slice(e.xs, b);
    b.append(pick(s));
    print(b);                                   // [1, 2, 1, 2, 2]
    var c: i32[..] = [..4];
    from_grow(g, c);
    c.append(&n.xs);
    print(c, " ", c.cap);                       // [3, 4, 1, 2] 4
    var d: i32[..8] = [];
    from_fixed(f, d);
    d.append(&e.xs);
    d.append(pick(n.xs));
    print(d);                                   // [5, 6, 1, 2]
}
