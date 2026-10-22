// error: expected a value of type u16, got i64 (narrowing and sign changes require an explicit `as`)
// §3.1: a global with a written type is a value of that type, `~0` as
// much as any other constant (named_const_typed_global.goose).

let T: i64 = ~0;

fn main() {
    let z: u16 = T;
    print(z);
}
