// error: storing a reference into p, which holds a grow-shrink array
// k keeps a reference to a field of the element of the slice its
// parameter's slot holds. Where that slice's storage holds a grow-shrink
// array of Pts, as a's does, nothing kept can lie in it; where it holds one
// of i64s, as b's does, the field could, so it is never stored (§5.2). Each
// call is checked for the arrays its own slice's storage holds, whichever
// comes first.
struct E { a: i64, x: f64 }
struct Pt { x: f64 }
struct BagA { es: E[2], items: Pt[>..<] }
struct BagB { es: E[2], nums: i64[>..<] }
fn k(p: E[:]&, loc: (i64&)[>..]&) { loc.push(p[0].a); }
fn main() {
    var a = BagA { es: [E { a: 1, x: 1.0 }, E { a: 2, x: 2.0 }], items: [Pt { x: 1.0 }] };
    var b = BagB { es: [E { a: 3, x: 3.0 }, E { a: 4, x: 4.0 }], nums: [7] };
    var loc: (i64&)[>..] = [];
    var s1 = a.es[..];
    k(s1, loc);
    var s2 = b.es[..];
    k(s2, loc);
    print(loc.len);
}
