// error: cannot infer the element type of []: a [] branch takes none from the other branches
// A [] does not adapt to another branch's type as a constant does (§6.4):
// with no destination to give the construct a type, it has none (§4.2).
fn main() {
    let c = true;
    print(if c { [] } else { [1, 2] });
}
