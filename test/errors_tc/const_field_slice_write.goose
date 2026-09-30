// error: cannot assign through this path (const, or a read-only instantiation
// A slice of a field of a const array type is a `const i64[:]` (§9.5).
struct Tally { counts: const i64[3] }
fn main() {
    var t = Tally { counts: [1, 2, 3] };
    let v = t.counts[..];
    v[0] = 9;
    print(t.counts);
}
