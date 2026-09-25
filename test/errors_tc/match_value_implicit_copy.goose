// error: short is not fixed-size and is not copied implicitly
// A match arm naming a variable-size variable is copied into the match's value
// only through copy() (§4.1).
fn main() {
    let long: u8[] = "long";
    let short: u8[] = "ab";
    let w = match short.len { 2 => short, _ => long };
    print(w);
}
