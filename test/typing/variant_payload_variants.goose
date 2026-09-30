// A payload may hold a variant type of its own ADT by value, one declared
// after it too, as long as no variant ends up holding itself (§3.4): a
// variant's payload is validated where it is first needed.
enum L { A { x: L.C, y: L.B }, B { y: i64 }, C { z: L.B } }
enum G<T> { A { x: G<T>.B }, B { y: T } }
fn main() {
    let a: L = L.A { x: L.C { z: L.B { y: 1 } }, y: L.B { y: 2 } };
    print(a);
    match a { A p => print(p.x.z.y + p.y.y), _ => print(0) }
    let g: G<i64> = G<i64>.A { x: G<i64>.B { y: 3 } };
    print(g);
}
