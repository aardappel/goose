// error: constant 300 does not fit u8
// §3.1: a named constant adapts only to a type its value fits, as a literal
// does.

let BIG = 300;
struct S { x: u8 }

fn main() {
    let s = S { x: BIG };
    print(s.x);
}
