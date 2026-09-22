// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
// A compound assignment of the whole var is its binding's business; one of
// an element writes the read-only contents (§9.5).
fn main() {
    var counts: const i64[3] = [0, 0, 0];
    counts = [1, 2, 3];
    counts[1] += 1;
    print(counts);
}
