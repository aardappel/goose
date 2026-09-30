// error: cannot infer the element type of []: the array literal around it gives it none
// Where no destination names its type, a fill's value gives the array its
// element type, and a [] has none to give (§4.2).
fn main() {
    let x = [[]; 2];
    print(x.len);
}
