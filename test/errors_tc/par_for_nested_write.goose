// error: cannot assign count in a par for: count is a variable of the enclosing function
// R1 (§4.3): a nested function the body calls is held to the body's rules
// for the variables it names from outside it.
fn main() {
    var count = 0;
    fn bump() { count = count + 1; }
    bump();
    par for k in 3 { bump(); }
    print(count);
}
