// A block's value is a new value, not the storage its break named.
fn main() {
    var x = 1;
    let r .= block { break x; };
    r = 5;
    print(x);
}
