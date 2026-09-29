// error: expected a value of type i64, got i64?
// So is a function it is handed to as a value: apply, checked with get where
// p is narrowed, is checked again where it is not (§3.8, §7.6).
fn apply<F>() -> i64 { F() }
let g = block {
    var x = 5;
    var p: i64? = x;
    fn get() -> i64 { p }
    var t = 0;
    if p { t = apply(get); }
    t += apply(get);
    t
};
fn main() { print(g); }
