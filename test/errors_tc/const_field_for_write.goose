// error: cannot write through this reference: non-writable provenance
// A for loop binding the elements of a field of a const array type by
// reference binds read-only ones (§6.5, §9.5).
struct Tally { counts: const i64[3] }
fn main() {
    var t = Tally { counts: [1, 2, 3] };
    for &c in t.counts { c = 0; }
    print(t.counts);
}
