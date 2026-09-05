// An integer literal does not become a float on its own (§6.3); the error
// says what to write.
fn main() {
    let x = 2.5;
    print((x + 1.5) / 2);
}
