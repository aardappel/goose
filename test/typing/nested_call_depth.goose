// A call nested as an argument is checked for the outer call's resolution
// and again against its parameter. The first check leaves the nested call's
// own phase 2 to the second (Phase2Waits), so the checks grow with the
// square of the nesting depth rather than doubling with each level.
struct Box { v: i64 }
fn g(x: i64) -> i64 { x + 1 }
fn add(a: i64, b: i64) -> i64 { a + b }
fn up(b: Box) -> Box { Box { v: b.v + 1 } }

fn main() {
    let x = 0;
    let nested =
        g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(
        g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(g(x
        ))))))))))))))))))))
        ))))))))))))))))))));
    let chained = x.g().g().g().g().g().g().g().g().g().g().g().g().g().g().g().g().g().g().g().g()
        .g().g().g().g().g().g().g().g().g().g().g().g().g().g().g().g().g().g().g().g();
    let paired =
        add(add(add(add(add(add(add(add(add(add(add(add(add(add(add(add(add(add(add(add(
        x, g(x)), g(x)), g(x)), g(x)), g(x)), g(x)), g(x)), g(x)), g(x)), g(x)),
        g(x)), g(x)), g(x)), g(x)), g(x)), g(x)), g(x)), g(x)), g(x)), g(x));
    let boxed = Box { v: x }.up().up().up().up().up().up().up().up().up().up().up().up().up()
        .up().up().up().up().up().up().up().up().up().up().up().up().up().up().up().up().up()
        .up().up().up().up().up().up().up().up().up().up();
    print(nested, " ", chained, " ", paired, " ", boxed.v);
}
