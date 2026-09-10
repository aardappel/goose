// The contents of a const value are read-only through every path (§9.5).
fn main() {
    const xs = [1, 2, 3];
    xs[0] = 9;
}
