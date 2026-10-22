// Equality of variable-size values (§4.5) whose bytes hold more than the
// value: a runtime-capacity limited array's capacity and its slots past the
// length, alone, in a struct, as an element and in an ADT's payload, compare
// as its length and live elements; and an explicit pad (§3.2), after a
// variable-size field or before one, in an element and in a variant's
// payload, is stepped over and never compared.
struct S { tag: u8, v: i64[..] }
enum L { Lim { v: i64[..] }, None }
struct P { items: i64[], pad 2, b: i32 }
struct M { a: u8, pad 3, b: i32, items: i64[] }
enum E { A { items: u8[], pad 3, n: i64 }, B { k: i64 } }

fn main() {
    var a: i64[..] = [1, 2, 3];
    a.pop();
    let b: i64[..] = [1, 2];
    let c: i64[..] = [1, 3];
    print(a == b, " ", a != b, " ", a == c, " ", b == a);
    var s = S { tag: 1, v: [1, 2, 3] };
    s.v.pop();
    let t = S { tag: 1, v: [1, 2] };
    let u = S { tag: 2, v: [1, 2] };
    print(s == t, " ", s == u);
    let xs: (i64[..])[] = [copy(b), copy(a)];
    let ys: (i64[..])[] = [copy(a), copy(b)];
    let zs: (i64[..])[] = [copy(a), copy(c)];
    var caps = 0;
    for v in ys { caps = caps * 10 + v.cap; }
    print(xs == ys, " ", xs == zs, " ", caps);
    let l: L.. = L.Lim { v: copy(a) };
    let k: L.. = L.Lim { v: [1, 2] };
    print(l == k);

    // p and q, and e and f, differ only in the top bytes of their last
    // field, which a walk not stepping over the pad before it never reaches.
    let p = P { items: [1], b: 0x10000 };
    let q = P { items: [1], b: 0x20000 };
    let r = P { items: [1], b: 0x10000 };
    print(p == q, " ", p == r, " ", p != q);
    let ps: P[] = [copy(p), copy(r)];
    let qs: P[] = [copy(p), copy(q)];
    let rs: P[] = [copy(r), copy(p)];
    print(ps == qs, " ", ps == rs);
    let e: E.. = E.A { items: [1, 2], n: 1 << 40 };
    let f: E.. = E.A { items: [1, 2], n: 2 << 40 };
    let g: E.. = E.A { items: [1, 2], n: 1 << 40 };
    print(e == f, " ", e == g);
    let x = M { a: 1, b: 2, items: [3, 4] };
    let y = M { a: 1, b: 2, items: [3, 4] };
    let z = M { a: 1, b: 3, items: [3, 4] };
    let w = M { a: 1, b: 2, items: [3, 5] };
    print(x == y, " ", x == z, " ", x == w);
}
