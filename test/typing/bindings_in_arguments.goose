// warning: redundant &: t binds by reference without it
// warning: redundant &: elements of this type bind by reference without it
// A call checks its arguments twice: bottom-up, then against the chosen
// parameter types. A declaration or `for` inside an argument (in a block,
// an if, a match or a loop) is checked with it, and binds the same both
// times: a non-fixed lvalue by reference, which is no redundant & since
// none was written (§4.1), and the uses read what the declaration bound.
// A redundant & that was written warns, once.
struct Txt { body: u8[] }
struct Bag { items: i64[>..<] }
enum Pick { One, Two }
enum Shape { Circle { r: i64 }, Point { k: i64 } }

fn id(n: i64) -> i64 { n }
fn len1(t: Txt) -> i64 { t.body.len }
fn plus(a: i64, b: i64) -> i64 { a + b }
fn twice(n) -> i64 { n * 2 }
fn size(s: Shape.Circle, n: i64) -> i64 { s.r + n }
fn size(s: Shape.Point, n: i64) -> i64 { s.k * n }
fn split(a: i64, b: i64) -> i64, i64 { return a / b, a % b; }

fn main() {
    let k = 7;
    print(id(block { let x = k + 1; break x * 2; }), " ",
          id(if k > 0 { var y = k; y += 3; y } else { 0 }), " ",
          id(block { let q, r = split(k, 3); break q * 10 + r; }), " ",
          id(block { var z: i64; z = k; break z; }));
    // A non-fixed lvalue binds by reference: in a block, an if, a match and a
    // loop, and in a UFCS call's argument, a nested call, an untyped
    // parameter and a dispatched call.
    let t = Txt { body: "hey" };
    let p: Pick = Pick.Two;
    let c: Shape = Shape.Circle { r: 2 };
    print(id(block { let a = t; break a.body.len; }), " ",
          id(if t.body.len > 0 { let a = t; a.body.len } else { 0 }), " ",
          id(match p { One => 0, _ => block { let a = t; break a.body.len; } }), " ",
          id(loop { let a = t; break a.body.len; }));
    print(k.plus(block { let a = t; break a.body.len; }), " ",
          id(id(block { let a = t; break a.body.len; })), " ",
          twice(block { let a = t; break a.body.len; }), " ",
          size(c, block { let a = t; break a.body.len; }));
    print(id(block { let r .= t; break r.body.len; }), " ",
          id(block { let r: Txt& = t; break r.body.len; }));
    // A copy of what it binds is a value, for a value parameter.
    print(len1(block { let a = t; break copy(a); }), " ",
          len1(if t.body.len > 0 { let a = t; copy(a) } else { Txt { body: "" } }));
    // The binding is the variable itself: a push through it grows the bag.
    var bag = Bag { items: [1, 2] };
    print(id(block { let b = bag; b.items.push(9); break b.items.len; }), " ", bag.items.len);
    // A `for` binds non-fixed elements by reference.
    let ts: Txt[] = [Txt { body: "hey" }, Txt { body: "yo" }];
    print(id(block { var n = 0; for u in ts { n += u.body.len; } break n; }));
    // The two warnings: an & that was written.
    print(id(block { let a = &t; break a.body.len; }), " ",
          id(block { var n = 0; for &u in ts { n += u.body.len; } break n; }));
}
