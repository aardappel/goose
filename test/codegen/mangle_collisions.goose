// Types that C would name alike are told apart (Mangle): an ADT of a
// variable-mode ADT and the variable-mode ADT of the fixed-mode one
// (`E<F..>`, `E<F>..`), an ADT of a variant and a variant of the ADT of
// that variant's ADT (`H<G.B>`, `H<G>.B`), and a generic instance and a
// type whose name spells it out (`A<B>` and `A_g_B`, `K<B>` and `K_g_B`):
// their C types, tag constants, comparisons and rendering.
enum F { X { n: i64 }, Y }
enum E<T> { R { r: T? }, N }
enum G { A { n: i64 }, B { m: i64 } }
enum H<T> { A { t: T }, B { u: i64 } }
struct B { n: i64 }
struct A<T> { t: T, k: i64 }
struct A_g_B { x: u8, y: u8 }
enum K<T> { V { t: T }, W }
enum K_g_B { V { x: u8 }, W }

fn main() {
    let fx: F = F.X { n: 1 };
    let fv: F.. = F.X { n: 2 };
    let a: E<F..> = E<F..>.R { r: fv };
    let a2: E<F..> = E<F..>.R { r: fv };
    let b: E<F..> = E<F..>.N {};
    let c: E<F>.. = E<F>.R { r: fx };
    let c2: E<F>.. = E<F>.R { r: fx };
    let d: E<F>.. = E<F>.N {};
    print(a == b, " ", a == a2, " ", c == d, " ", c == c2);
    print(a, " ", c);

    let x: H<G.B> = H<G.B>.A { t: G.B { m: 1 } };
    let y: H<G> = H<G>.B { u: 2 };
    let z: H<G>.B = H<G>.B { u: 3 };
    print(x, " ", y, " ", z.u);

    let p = A<B> { t: B { n: 5 }, k: 6 };
    let q = A_g_B { x: 1, y: 2 };
    print(p.t.n, " ", p.k, " ", q.x, " ", q.y, " ", p == A<B> { t: B { n: 5 }, k: 6 }, " ",
          q == A_g_B { x: 1, y: 3 });
    let k1: K<B> = K<B>.V { t: B { n: 7 } };
    let k2: K_g_B = K_g_B.V { x: 8 };
    print(k1, " ", k2);
}
