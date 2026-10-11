// error: the array size relies on name keeping its initializer's length, but name is a var of type const u8[:], which does not fix it
// §3.3: a var's `.len` is a constant only where its type fixes it, as a
// string literal's `const u8[:]` does not: an assignment may change it.
var name = "goose";

fn main() {
    let a: u8[name.len] = "geese";
    print(a, name);
}
