// A fixed value over CodeGen::FIXED_PTR_ABI_LIMIT may be lent to a
// read-only parameter (CodeGen::EmitArg, StableArg) only when nothing
// else writes it during the call. A later argument that runs statements,
// and a dispatch, which reads every argument before the switch, both
// count as later, so the variable is copied. `recursive` keeps the
// callees as calls at -O2. Lending anyway lets the later write, or the
// `var` arm, change the value the callee reads (§7.2).
import std;

struct Big { a: i64 = 0, b: i64 = 0, rest: i64[40] = [0; 40] }

recursive fn peek(d: Big, k: i64) -> i64 { d.a * 1000 + k }

enum Shape {
    Thin { n: i64 },
    Wide { w: i64 },
}

recursive fn tag(s: Shape.Thin, d: Big) -> i64 { d.a + s.n }
recursive fn tag(s: Shape.Wide, var d: Big) -> i64 { d.a = 50; d.a + s.w }

fn main() {
    var v = Big { a: 1, .. };
    print("later ", peek(v, block { v.a = 5; 7 }), " ", v.a);
    var q = Big { a: 1, .. };
    var shapes: Shape[>..] = [];
    shapes.push(Shape.Thin { n: 2 });
    shapes.push(Shape.Wide { w: 3 });
    for s in shapes { print("dispatch ", tag(s, q), " ", q.a); }
}
