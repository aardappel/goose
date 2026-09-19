// error: binding h to a value holding references rooted at a temporary, which does not outlive it
// A holder's contents bound it like a view would: this one points into a
// call's result.
struct V { s: const u8[:] }
fn mk() -> u8[] { "hello" }
fn wrap(xs: const u8[:]) -> V { V { s: xs } }
fn main() {
    var h = wrap(mk());
    print(h.s);
}
