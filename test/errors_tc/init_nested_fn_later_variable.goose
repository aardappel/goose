// error: unknown identifier: j
// A function declared in a global initializer names the variables in scope
// where it is declared, not one its block declares further on (§7.5).
let g = block {
    fn h() -> i64 { j }
    let j = 5;
    h()
};
fn main() { print(g); }
