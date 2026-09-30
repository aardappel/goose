// error: cannot infer the element type of []: an omitted result type is taken from the value returned
// An omitted result type is taken from the value returned, as an un-annotated
// let takes its initializer's (§7.1): a [] gives it no element type, whatever
// the caller wants.
fn none() { [] }
fn main() {
    let x: u8[] = none();
    print(x.len);
}
