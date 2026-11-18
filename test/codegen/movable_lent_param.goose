// A variable named once may be handed to a by-pointer parameter in place
// of a copy (CodeGen::MovableArg). A read-only parameter may not: its
// callers can lend it their own variable (CodeGen::ParamUnwritten), and
// ConsumedParam only sees a plain call, not a dispatch whose `var` arm
// writes. The copy is made at the dispatch. `recursive` keeps the
// callees as calls at -O2. Handing the lent variable on lets that arm
// change the caller's value (§7.2).
import std;

struct Big { a: i64 = 0, b: i64 = 0, rest: i64[40] = [0; 40] }

enum Shape {
    Thin { n: i64 },
    Wide { w: i64 },
}

recursive fn tag(s: Shape.Thin, d: Big) -> i64 { d.a + s.n }
recursive fn tag(s: Shape.Wide, var d: Big) -> i64 { d.a = 50; d.a + s.w }
recursive fn relay(s: Shape, d: Big) -> i64 { tag(s, d) }

fn main() {
    let q = Big { a: 1, .. };
    var shapes: Shape[>..] = [];
    shapes.push(Shape.Wide { w: 3 });
    shapes.push(Shape.Thin { n: 2 });
    for s in shapes { print("relay ", relay(s, q), " ", q.a); }
}
