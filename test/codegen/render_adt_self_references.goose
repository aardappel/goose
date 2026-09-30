// runtime-debug: print, str and format follow an ADT's links to the ADT
// itself as deep as they go (§3.7), in fixed and in variable mode and from
// one to the other, through plain, optional, self-relative and pool links
// and slices, generic too, and through types holding the ADT that it
// refers to: every level is rendered, a fixed-mode value overwritten in
// place by another variant as what it is then. A fixed-mode payload's
// self-relative links are read by nothing else (§3.5, §3.9).

enum L { Nil, Cons { v: i64, next: L&? } }
enum N { End, Link { v: i64, next: N& } }
// Variable mode, linking to its own variable and to its fixed mode.
enum V { Nil, Cons { name: u8[], next: V..? } }
enum M { Nil, Cons { v: i64, next: M? } }
// Fixed mode linking to its variable mode.
enum F { Nil, Cons { v: i64, next: F..? } }
// Self-relative links, and links into a pool.
enum S { Nil, Cons { v: i64, next: S&<u32>? } }
enum P { Nil, Cons { v: i64, next: P&<u16 in pool>? } }
var pool: P[>..] = [];
enum T { Leaf { v: i64 }, Node { kids: T[:] } }
enum G<X> { Nil, Cons { v: X, next: G<X>? } }
// The first of each pair refers to the second, which contains it by value.
enum E1 { A { v: i64, e: E2? }, X }
enum E2 { B { e: E1 }, Y }
struct SA { v: i64, b: SB? }
struct SB { a: SA }
// A generic struct is built where it is first used, here behind the link
// of an enum it holds in variable mode.
enum W { A { v: i64, s: WS<i64>? }, B { t: u8[] } }
struct WS<X> { e: W.., t: X }

recursive fn sum(l: L&) -> i64 {
    match l {
        Cons c => {
            let n = c.next;
            guard n else { return c.v; }
            return c.v + sum(n);
        },
        Nil => 0,
    }
}

fn main() {
    var c = L.Nil;
    var b: L = L.Cons { v: 2, next: c };
    var a: L = L.Cons { v: 1, next: b };
    print(a, " ", sum(a));
    print(str("[", b, "]"));
    var buf: u8[>..] = [];
    format(buf, a, "|", c);
    print(buf);
    // A fixed-mode value is overwritten in place, by another variant too:
    // what refers to it sees the new one.
    c = L.Cons { v: 3, next: null };
    print(a, " ", sum(a));
    b = L.Nil;
    print(a, " ", sum(a));
    var end = N.End;
    var n1: N = N.Link { v: 1, next: end };
    print(N.Link { v: 2, next: n1 });

    let vn: V.. = V.Nil;
    let vb: V.. = V.Cons { name: "b", next: vn };
    let va: V.. = V.Cons { name: "a", next: vb };
    print(va);
    var mc: M = M.Cons { v: 4, next: null };
    let mv: M.. = M.Cons { v: 5, next: mc };
    print(mv);
    let fv: F.. = F.Cons { v: 6, next: null };
    let ff: F = F.Cons { v: 7, next: fv };
    print(ff);

    var ss: S[>..] = [];
    ss.push(S.Nil);
    ss.push(S.Cons { v: 1, next: ss[0] });
    ss.push(S.Cons { v: 2, next: ss[1] });
    print(ss[2]);
    print(ss);
    pool.push(P.Nil);
    pool.push(P.Cons { v: 1, next: pool[0] });
    pool.push(P.Cons { v: 2, next: pool[1] });
    print(pool[2]);

    var leaves: T[>..] = [];
    leaves.push(T.Leaf { v: 1 });
    leaves.push(T.Leaf { v: 2 });
    var mid: T[>..] = [];
    mid.push(T.Node { kids: leaves });
    let root = T.Node { kids: mid };
    print(root);

    var gn: G<f64> = G<f64>.Nil {};
    let gc: G<f64> = G<f64>.Cons { v: 2.5, next: gn };
    print(gc);

    var y = E2.Y;
    var e1 = E1.A { v: 1, e: y };
    var e2: E2 = E2.B { e: e1 };
    print(E1.A { v: 2, e: e2 });
    var sb = SB { a: SA { v: 1, b: null } };
    print(SA { v: 2, b: sb });
    var ws = WS<i64> { e: W.B { t: "s" }, t: 1 };
    let wa: W.. = W.A { v: 2, s: ws };
    print(wa);
}
