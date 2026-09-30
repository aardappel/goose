// runtime-debug: an ADT's payloads holding self-relative references are read
// by reference, which only variable mode offers (§3.5, §3.9): a match binds
// them with `&` (§8.1) and case functions take the variant by reference
// (§8.2), in a list, in a tree whose links are never null, in a ring started
// with `self` (§3.9), and where a struct holds its links in a variable-mode
// field. Links `in pool` copy (§3.9), so a fixed-mode ADT holding them binds
// its payloads by value, dispatches to by-value cases, is read by index and
// is overwritten in place by another variant.

enum S { Nil, Cons { v: i64, next: S..&<u32>? } }

recursive fn total(s: S..&) -> i64 {
    match s {
        Cons &c => {
            let n = c.next;
            guard n else { return c.v; }
            return c.v + total(n);
        },
        Nil => 0,
    }
}

recursive fn count(c: S.Cons&) -> i64 {
    let n = c.next;
    guard n else { return 1; }
    return 1 + count(n);
}
fn count(n: S.Nil&) -> i64 { 0 }

enum Tree { Leaf { v: i64 }, Node { l: Tree..&<u16>, r: Tree..&<u16> } }

recursive fn tsum(t: Tree..&) -> i64 {
    match t {
        Leaf &f => f.v,
        Node &n => tsum(n.l) + tsum(n.r),
    }
}

recursive fn depth(n: Tree.Node&) -> i64 {
    let l = depth(n.l);
    let r = depth(n.r);
    return 1 + (if l > r { l } else { r });
}
fn depth(f: Tree.Leaf&) -> i64 { 0 }

enum R { Ring { v: i64, next: R..&<u16> }, Stop }

recursive fn around(r: R..&, n: i64) -> i64 {
    if n == 0 { return 0; }
    match r {
        Ring &g => g.v + 10 * around(g.next, n - 1),
        Stop => 0,
    }
}

// A struct linked to others of its kind through an ADT field.
struct B { w: i64, l: L.. }
enum L { End, Next { b: B&<u32> } }

recursive fn chain(b: B&) -> i64 {
    match b.l {
        Next &n => b.w + 10 * chain(n.b),
        End => b.w,
    }
}

var ppool: P[>..] = [];
enum P { Nil, Cons { v: i64, next: P&<u16 in ppool>? } }

recursive fn psum(c: P.Cons) -> i64 {
    let n = c.next;
    guard n else { return c.v; }
    return c.v + psum(n);
}
fn psum(n: P.Nil) -> i64 { 0 }

fn main() {
    // A payload-less variant of S is a variable-mode S.
    let e = S.Nil;
    print(total(e), " ", count(e));

    var ss: S..[>..] = [];
    let s0 .= ss.push(S.Nil);
    let s1 .= ss.push(S.Cons { v: 1, next: s0 });
    let s2 .= ss.push(S.Cons { v: 2, next: s1 });
    let s3 .= ss.push(S.Cons { v: 3, next: s2 });
    print(total(s3), " ", count(s3), " ", count(s0));

    var ts: Tree..[>..] = [];
    let a .= ts.push(Tree.Leaf { v: 1 });
    let b .= ts.push(Tree.Leaf { v: 2 });
    let c .= ts.push(Tree.Node { l: a, r: b });
    let d .= ts.push(Tree.Leaf { v: 4 });
    let root .= ts.push(Tree.Node { l: c, r: d });
    print(tsum(root), " ", depth(root), " ", depth(d));

    var rs: R..[>..] = [];
    let r0 .= rs.push(R.Ring { v: 1, next: self });
    let r1 .= rs.push(R.Ring { v: 2, next: r0 });
    print(around(r1, 4), " ", around(r0, 2));

    var bs: B[>..] = [];
    let b0 .= bs.push(B { w: 1, l: L.End });
    let b1 .= bs.push(B { w: 2, l: L.Next { b: b0 } });
    let b2 .= bs.push(B { w: 3, l: L.Next { b: b1 } });
    print(chain(b2));

    ppool.push(P.Nil);
    ppool.push(P.Cons { v: 10, next: ppool[0] });
    ppool.push(P.Cons { v: 20, next: ppool[1] });
    print(psum(ppool[2]));
    // What links to an element sees the variant it is overwritten with.
    ppool[1] = P.Nil;
    print(psum(ppool[2]));
    ppool[0] = P.Cons { v: 5, next: null };
    ppool[1] = P.Cons { v: 7, next: ppool[0] };
    print(psum(ppool[2]));
    match ppool[1] {
        Cons p => {
            let n = p.next;
            if n { print(p.v, " ", psum(n)); }
        },
        Nil => {},
    }
}
