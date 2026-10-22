// error: extern fn give cannot return Outer: a struct with pad passes by value
// A pad in a nested struct counts too. (spec §7.10)
struct Mixed { x: f32, pad 4, y: f64 }
struct Outer { m: Mixed }
extern "foreign" { fn give() -> Outer; }
fn main() {}
