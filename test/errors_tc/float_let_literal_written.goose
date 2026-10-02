// error: k is bound to a writable reference here, through which it may change, while the use as a literal at
// error: float_let_literal_written.goose:12 relies on it keeping its initializer's value, as a named constant
// §4.4, §6.3: the field takes the literal-like k rounded to f32, which relies
// on k keeping its initializer's value; scale could store any f64 in it,
// which an f32 takes only through an explicit `as`.

struct P { x: f32 }
fn scale(r: f64&) { r *= 1.0e300; }

fn main() {
    let k = 0.5;
    let p = P { k };
    scale(k);
    print(p.x, " ", k);
}
