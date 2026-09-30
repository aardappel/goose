// runtime-debug: an ADT's payload may refer to the ADT itself, in fixed
// and in variable mode, whatever the reference's spelling -- plain,
// optional, relative, a slice -- since a reference is fixed-size whatever
// it points at (§3.4). Only containing a type by value makes it part of
// the type: a type reached through a reference may contain the one
// referring to it, whichever of the two is declared first.

enum L { Nil, Cons { v: i64, next: L&? } }
enum N { End, Link { v: i64, next: N& } }
// Variable mode, linking to its own variable mode and to its fixed mode.
enum V { Nil, Cons { name: u8[], next: V..? } }
enum M { Nil, Cons { v: i64, next: M? } }
// Fixed mode linking to its variable mode.
enum F { Nil, Cons { v: i64, next: F..? } }
// Self-relative links, which only variable mode's payloads hold (§3.5),
// and links into a pool.
enum SV { Nil, Cons { v: i64, next: SV..&<u32>? } }
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

recursive fn sum_n(n: N&) -> i64 { match n { Link k => k.v + sum_n(k.next), End => 0 } }

recursive fn names(v: V..&) -> i64 {
    match v {
        Cons &c => {
            let n = c.next;
            guard n else { return c.name.len; }
            return c.name.len * 10 + names(n);
        },
        Nil => 0,
    }
}

recursive fn sum_m(m: M&) -> i64 {
    match m {
        Cons c => {
            let n = c.next;
            guard n else { return c.v; }
            return c.v + sum_m(n);
        },
        Nil => 0,
    }
}

recursive fn sum_sv(s: SV..&) -> i64 {
    match s {
        Cons &c => {
            let n = c.next;
            guard n else { return c.v; }
            return c.v + sum_sv(n);
        },
        Nil => 0,
    }
}

recursive fn sum_p(p: P&) -> i64 {
    match p {
        Cons c => {
            let n = c.next;
            guard n else { return c.v; }
            return c.v + sum_p(n);
        },
        Nil => 0,
    }
}

recursive fn leaves(t: T&) -> i64 {
    match t {
        Leaf l => l.v,
        Node n => {
            var s = 0;
            for k in n.kids { s += leaves(k); }
            s
        },
    }
}

recursive fn sum_g(g: G<f64>&) -> f64 {
    match g {
        Cons c => {
            let n = c.next;
            guard n else { return c.v; }
            return c.v + sum_g(n);
        },
        Nil => 0.0,
    }
}

recursive fn depth(e: E1&) -> i64 {
    match e {
        A a => {
            let n = a.e;
            guard n else { return 1; }
            return 1 + (match n { B b => depth(b.e), Y => 0 });
        },
        X => 0,
    }
}

fn main() {
    var c = L.Nil;
    var b: L = L.Cons { v: 2, next: c };
    var a: L = L.Cons { v: 1, next: b };
    print(sum(a));
    // A fixed-mode value is overwritten in place, by another variant too:
    // what refers to it sees the new one.
    c = L.Cons { v: 3, next: null };
    print(sum(a));
    b = L.Nil;
    print(sum(a));
    var end = N.End;
    var n1: N = N.Link { v: 4, next: end };
    let n2: N = N.Link { v: 5, next: n1 };
    print(sum_n(n2));

    let vn: V.. = V.Nil;
    let vb: V.. = V.Cons { name: "bb", next: vn };
    let va: V.. = V.Cons { name: "a", next: vb };
    print(names(va));
    var mc: M = M.Cons { v: 6, next: null };
    let mv: M.. = M.Cons { v: 7, next: mc };
    match mv {
        Cons m => {
            let n = m.next;
            if n { print(m.v, " ", sum_m(n)); }
        },
        Nil => {},
    }
    let fv: F.. = F.Cons { v: 8, next: null };
    let ff: F = F.Cons { v: 9, next: fv };
    match ff {
        Cons f => {
            let n = f.next;
            if n { match n { Cons &g => print(f.v, " ", g.v), Nil => {} } }
        },
        Nil => {},
    }

    var svs: SV..[>..] = [];
    let s0 .= svs.push(SV.Nil);
    let s1 .= svs.push(SV.Cons { v: 10, next: s0 });
    let s2 .= svs.push(SV.Cons { v: 20, next: s1 });
    print(sum_sv(s2));
    pool.push(P.Nil);
    pool.push(P.Cons { v: 30, next: pool[0] });
    pool.push(P.Cons { v: 40, next: pool[1] });
    print(sum_p(pool[2]));

    var ls: T[>..] = [];
    ls.push(T.Leaf { v: 1 });
    ls.push(T.Leaf { v: 2 });
    var mid: T[>..] = [];
    mid.push(T.Node { kids: ls });
    mid.push(T.Leaf { v: 4 });
    let root: T = T.Node { kids: mid };
    print(leaves(root));

    var gn: G<f64> = G<f64>.Nil {};
    let gc: G<f64> = G<f64>.Cons { v: 2.5, next: gn };
    print(sum_g(gc));

    var y = E2.Y;
    var e1 = E1.A { v: 1, e: y };
    var e2: E2 = E2.B { e: e1 };
    var top: E1 = E1.A { v: 2, e: e2 };
    print(depth(top));
    var sb = SB { a: SA { v: 1, b: null } };
    let sa = SA { v: 2, b: sb };
    let sab = sa.b;
    if sab { print(sa.v, " ", sab.a.v); }
    var ws = WS<i64> { e: W.B { t: "s" }, t: 1 };
    let wa: W.. = W.A { v: 2, s: ws };
    match wa {
        A w => {
            let s = w.s;
            if s { match s.e { B x => print(w.v, " ", x.t, " ", s.t), A => {} } }
        },
        B => {},
    }
}
