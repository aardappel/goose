// error: cannot infer the element type of []: the array literal around it gives it none
// Where no destination names its type, an array literal's first element
// gives the others their type, and a [] has none to give (§4.2).
fn main() {
    let x = [[]];
    print(x.len);
}
