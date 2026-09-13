// error: .= binds a reference: the initializer must be a reference, a slice, or storage (a variable, field or element)
// A block's value is a new value, not the storage its break named.
fn main() {
    var x = 1;
    let r .= block { break x; };
    r = 5;
    print(x);
}
