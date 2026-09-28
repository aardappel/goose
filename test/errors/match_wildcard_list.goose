// error: _ cannot be listed with other patterns
// _ matches every value on its own (§8.1), so it begins no list of patterns.
fn main() {
    let x = 3;
    print(match x { _, 1 => 1 });
}
