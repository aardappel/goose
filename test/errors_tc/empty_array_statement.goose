// error: cannot infer the element type of []: a statement gives it none
// A statement's value goes nowhere, so nothing gives a [] there an element
// type (§4.2).
fn main() {
    [];
    print(1);
}
