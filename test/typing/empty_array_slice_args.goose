// A [] passed to a slice parameter is a temporary array of the parameter's
// elements that the slice views whole (§4.2), as a literal with elements is:
// an empty one, whatever the element type, at every kind of call. Overload
// resolution finds the parameter's type; the argument is checked at it
// before the specialization is keyed on where the slice points.
struct P { x: i64, y: i64 }
enum Shape { Circle { r: i64 }, Square { s: i64 } }

fn n(xs: const i64[:]) -> i64 { xs.len }
fn sum(xs: i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    t
}
fn text(s: const u8[:]) -> i64 { s.len }
fn points(ps: const P[:]) -> i64 { ps.len }
fn rows(rs: const i64[3][:]) -> i64 { rs.len }
fn floats(fs: const f32[:]) -> i64 { fs.len }
fn refs(rs: (const i64&)[:]) -> i64 {
    var t = 0;
    for r in rs { t += r; }
    rs.len * 100 + t
}
fn words(ws: (const u8[:])[:]) -> i64 {
    var t = 0;
    for w in ws { t += w.len; }
    ws.len * 100 + t
}
fn two(a: const i64[:], b: const i64[:]) -> i64 { a.len * 10 + b.len }
fn none(xs: const i64[:] = []) -> i64 { xs.len + 100 }
fn size<T>(xs: const T[:]) -> i64 { xs.len + 200 }
fn id(xs: const i64[:]) -> const i64[:] { xs }
fn area(c: Shape.Circle, xs: const i64[:]) -> i64 { c.r * 3 + xs.len }
fn area(q: Shape.Square, xs: const i64[:]) -> i64 { q.s * q.s + xs.len }
recursive fn down(xs: const i64[:], k: i64) -> i64 {
    if k == 0 { return xs.len; }
    down([], k - 1) + xs.len
}
fn apply<F>() -> i64 { F([]) * 10 + F([4, 5]) }

fn main() {
    let a = 5;
    let ra: (const i64&)[2] = [a, a];
    print(n([]), " ", sum([]), " ", text([]), " ", points([]), " ", rows([]), " ",
          floats([]));                                               // 0 0 0 0 0 0
    print(refs([]), " ", refs(ra), " ", words([]), " ", words(["ab", "c"]));   // 0 210 0 203
    print(two([], [1, 2]), " ", two([3], []), " ", two([], []));      // 2 10 0
    print(sum([]), " ", [].sum(), " ", sum([4, 5]));                  // 0 0 9
    print(none(), " ", none([]), " ", size<i64>([]), " ", size<P>([]));   // 100 100 200 200
    print(id([]).len, " ", n([n([]), n([7])]));                       // 0 2
    let c = Shape.Circle { r: 2 };
    let q = Shape.Square { s: 3 };
    print(area(c, []), " ", area(q, []), " ", area(q, [1]));          // 6 9 10
    print(down([1, 2, 3], 3), " ", down([], 2));                        // 3 0
    print(apply() { v: const i64[:] => v.len });                       // 2
    fn inner(xs: const u8[:], k: i64) -> i64 { xs.len + k }
    print(inner([], a), " ", inner("abc", a));                          // 5 8
}
