// error: cannot modify through this path (const, or a read-only instantiation, §9.5)
// A var of a const array type is assigned as a whole, and its elements stay
// read-only: ++ on one would write the contents (§9.5).
fn main() {
    var counts: const i64[3] = [0, 0, 0];
    counts = [1, 2, 3];
    counts[1]++;
    print(counts);
}
