// A varint field is storage of type varint (§3.6), so at a destination of
// type varint& it binds by reference without `&`, as any lvalue does at a
// reference to its own type (§3.8, §4.1): a parameter, a generic `T&` one,
// one whose type argument is a reference, a result, an annotated binding,
// a reference field, an array literal's element, a push, a `.=` binding
// and rebind, an identity comparison, and each branch of a construct at
// such a destination, a reference to such a field too; the field reached
// through a reference, a nested struct and a variant's payload. Such a
// reference is read-only. What the field loads is the i64 it decodes to:
// an un-annotated binding or an untyped parameter takes a copy of that,
// which is its own variable, and `&` keeps the reference there instead.
struct V { n: varint, w: i64 }
struct Outer { tag: u8, v: V }
struct Holder { r: const varint& }
enum Shape { Pts { q: varint, k: i64 }, Nothing }

fn byref(r: varint&) -> i64 { r }
fn byconst(r: const varint&) -> i64 { r * 2 }
fn generic<T>(x: T&) -> i64 { x + 1 }
fn explicit<T>(x: T) -> i64 { x - 1 }
fn field(v: V&) -> varint& { return v.n; }
fn nested(o: Outer&) -> const varint& { return o.v.n; }
fn same(x, y: const varint&) -> bool { x .== y }
fn payload(s: Shape..&) -> i64 {
    match s {
        Pts p   => byref(p.q) + p.k,
        Nothing => 0,
    }
}

fn main() {
    var v = V { 300, 4 };
    let u = V { -7000, 5 };
    let o = Outer { tag: 1, v: V { 64, 6 } };
    print(byref(v.n), " ", byconst(u.n), " ", generic(v.n));     // 300 -14000 301
    print(explicit<const varint&>(u.n), " ", field(v));          // -7001 300
    print(nested(o), " ", byref(o.v.n));                         // 64 64

    let a: const varint& = v.n;
    let h = Holder { r: u.n };
    let rs: (const varint&)[] = [v.n, u.n, o.v.n];
    var more: (const varint&)[>..] = [];
    more.push(u.n);
    more.push(v.n);
    print(a, " ", h.r, " ", rs, " ", more);                      // 300 -7000 [300, -7000, 64] [-7000, 300]

    var q .= v.n;
    print(q, " ", q .== v.n, " ", q .== a);                      // 300 true true
    q .= u.n;
    print(q, " ", q .== u.n, " ", q .!= v.n, " ", h.r .== q);    // -7000 true true true
    let pick: const varint& = if v.w > u.w { v.n } else { u.n };
    print(pick, " ", pick .== u.n);                              // -7000 true
    print(byconst(if v.w > u.w { a } else { h.r }), " ",
          byconst(if v.w < u.w { a } else { h.r }));              // -14000 600

    var sh: Shape.. = Shape.Pts { q: 12, k: 3 };
    print(payload(sh));                                          // 15

    var c = v.n;
    c += 1;
    let e = &v.n;
    print(c, " ", v.n, " ", e, " ", e .== v.n);                  // 301 300 300 true
    print(same(&v.n, v.n), " ", same(&u.n, v.n));                // true false
}
