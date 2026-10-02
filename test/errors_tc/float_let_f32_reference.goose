// error: no matching overload for call to bump
// §3.8, §6.3: a literal-like `let` is f64 storage; a use rounds its value to
// f32 as a literal's, but a reference to it is an f64&, never an f32&.

fn bump(x: f32&) { x += 1.0; }

fn main() {
    let k = 0.5;
    let r: f32 = k;
    bump(k);
    print(r, " ", k);
}
