// error: returning a reference rooted in y, which dies with this function
// A payload matched out of a by-value ADT parameter is a copy of the caller's
// value: a slice read back out of it is bounded by the parameter's holder
// class (§9.5).
enum E { Txt { s: const u8[:] }, Nothing { k: i64 } }
fn get(e: E) -> const u8[:] { match e { Txt t => t.s, Nothing n => abort("none") } }
fn mk() -> const u8[:] {
    var y: u8[>..] = [];
    for i in 18 { y.push('a' + i as u8); }
    get(E.Txt { s: y[..] })
}
fn main() { let s = mk(); print(s); }
