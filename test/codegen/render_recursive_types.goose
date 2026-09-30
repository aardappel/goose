// runtime-debug: print, str and format follow references as deep as they go
// (§3.7), into values of the very type holding them too: every level of a
// type that reaches itself through references or slices is rendered, each
// by a call of its own, for plain and optional references, relative links,
// slices, generic, resizable and ADT types, and mutual recursion.

struct N { v: i64, next: N? }
var n2 = N { v: 2, next: null };
var n1 = N { v: 1, next: null };

// Mutual recursion: A -> B? -> A?.
struct A { x: i64, b: B? }
struct B { y: i64, a: A? }

struct L<T> { v: T, next: L<T>? }

// An ADT reaching itself through a struct, in fixed and in variable mode.
struct Box { l: Lst }
enum Lst { Nil, Cons { v: i64, next: Box? } }
struct VBox { e: Tree.. }
enum Tree { Leaf { s: u8[] }, Node { l: VBox?, r: VBox? } }

struct T { v: i64, kids: T[:] }

// Links into a pool, and self-relative ones.
struct PN { key: i64, link: PN&<u32 in P>? }
var P: PN[>..] = [];
struct SN { key: i64, link: SN&<u32>? }

// Resizable: a frame object, and a struct with a variable prefix, whose
// resizable tail reaches back to it through a reference.
struct R { v: i64, next: R?, name: u8[>..] }
struct V { name: u8[], w: W }
struct W { x: V?, items: i64[>..] }

fn show(n: N&) { print(n); }

fn main() {
    n1.next .= n2;
    print(n1);                          // N { 1, N { 2, null } }

    var c = N { v: 3, next: null };
    var b = N { v: 2, next: c };
    var a = N { v: 1, next: b };
    print(a);
    show(a);
    let o: N? = b;
    print(o, " ", default<N?>());       // N { 2, N { 3, null } } null
    let s = str("[", a, "]");
    print(s);
    var buf: u8[>..] = [];
    format(buf, a, "|", c);
    print(buf);
    var lim: u8[..40] = [];
    format(lim, b);
    print(lim);

    var a0 = A { x: 0, b: null };
    var b1 = B { y: 1, a: a0 };
    var a2 = A { x: 2, b: b1 };
    print(a2);                          // A { 2, B { 1, A { 0, null } } }
    print(b1);                          // B { 1, A { 0, null } }

    var l2 = L<i64> { v: 2, next: null };
    var l1 = L<i64> { v: 1, next: l2 };
    print(l1);                          // L<i64> { 1, L<i64> { 2, null } }

    var nil = Box { l: Lst.Nil };
    var two = Box { l: Lst.Cons { v: 2, next: nil } };
    let one = Lst.Cons { v: 1, next: two };
    print(one);                         // Cons { 1, Box { Cons { 2, Box { Lst.Nil } } } }

    var x = VBox { e: Tree.Leaf { s: "x" } };
    var y = VBox { e: Tree.Leaf { s: "y" } };
    var t = VBox { e: Tree.Node { l: x, r: y } };
    let top: Tree.. = Tree.Node { l: t, r: null };
    print(top);

    var leaves: T[>..] = [];
    leaves.push(T { v: 3, kids: leaves[0..0] });
    leaves.push(T { v: 4, kids: leaves[0..0] });
    var mid: T[>..] = [];
    mid.push(T { v: 2, kids: leaves });
    let root = T { v: 1, kids: mid };
    print(root);                        // T { 1, [T { 2, [T { 3, [] }, T { 4, [] }] }] }

    P.push(PN { key: 1, link: null });
    P.push(PN { key: 2, link: P[0] });
    P.push(PN { key: 3, link: P[1] });
    print(P[2]);                        // PN { 3, PN { 2, PN { 1, null } } }
    print(P);
    var sn: SN[] = [SN { key: 1, link: null }, SN { key: 2, link: null }];
    sn[1].link .= sn[0];
    print(sn);                          // [SN { 1, null }, SN { 2, SN { 1, null } }]

    var rb = R { v: 2, next: null, name: "bee" };
    var ra = R { v: 1, next: rb, name: "ay" };
    print(ra);                          // R { 1, R { 2, null, "bee" }, "ay" }
    var v0 = V { name: "zero", w: W { x: null, items: [7] } };
    var w1 = W { x: v0, items: [1, 2] };
    var v2 = V { name: "two", w: W { x: null, items: [] } };
    v2.w.x .= v0;
    print(w1);                          // W { V { "zero", W { null, [7] } }, [1, 2] }
    print(v2);                          // V { "two", W { V { "zero", W { null, [7] } }, [] } }
}
