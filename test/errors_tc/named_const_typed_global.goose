// error: expected a value of type u8, got i64 (narrowing and sign changes require an explicit `as`)
// §3.1: a global with a written type is a value of that type, no named
// constant adapting as a literal.

let SMALL: i64 = 3;
struct S { x: u8 }

fn main() {
    let s = S { x: SMALL };
    print(s.x);
}
