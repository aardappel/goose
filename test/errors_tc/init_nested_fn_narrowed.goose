// error: expected a value of type i64, got i64?
// A function declared in a global initializer is specialized by which of the
// initializer's optionals are narrowed where it is called: get, checked where
// p is narrowed, is checked again where it is not (§3.8, §7.5).
let g = block {
    var x = 5;
    var p: i64? = x;
    fn get() -> i64 { p }
    var t = 0;
    if p { t = get(); }
    t += get();
    t
};
fn main() { print(g); }
