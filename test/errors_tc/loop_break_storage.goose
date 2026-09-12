// A loop's value is a new value, not the storage its break named.
fn main() {
    var y = 1;
    let q .= loop { break y; };
    q = 6;
    print(y);
}
