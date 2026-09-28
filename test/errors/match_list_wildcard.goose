// error: _ cannot be listed with other patterns
// _ matches every value on its own (§8.1), so no list of integer patterns
// holds it, where it would read as a constant named _.
fn main() {
    let x = 3;
    print(match x { 1, _ => 1, _ => 0 });
}
