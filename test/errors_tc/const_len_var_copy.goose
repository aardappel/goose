// error: the array size relies on copy keeping its initializer's length, which rests on name's, but name is a var of type const u8[:]
// §3.3: copy's length is the one name had when copy was initialized, which
// an assignment to name before then may have changed.
var name = "goose";
let copy = name;

fn main() {
    let a: u8[copy.len] = "geese";
    print(a, copy);
}
