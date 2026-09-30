// error: cannot assign through this path (const, or a read-only instantiation
// A field of a const type bound by reference (§4.1) is read-only (§9.5).
struct Tally { counts: const i64[3] }
fn main() {
    var t = Tally { counts: [1, 2, 3] };
    let r .= t.counts;
    r[0] = 9;
    print(t.counts);
}
