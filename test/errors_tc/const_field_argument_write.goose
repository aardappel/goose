// error: cannot assign through this path (const, or a read-only instantiation
// A field of a const array type passed by reference is read-only, so poke's
// instantiation for it writes a read-only array (§9.5).
struct Tally { counts: const i64[3] }
fn poke(c: i64[3]&) { c[0] = 9; }
fn main() {
    var t = Tally { counts: [1, 2, 3] };
    poke(t.counts);
    print(t.counts);
}
