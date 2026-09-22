// error: cannot assign to let x
// `const x = e` is a let of a const type (§4.4): the binding is not
// reassigned, where a var of that type would be (§9.5).
fn main() {
    const x = "a";
    x = "b";
    print(x);
}
