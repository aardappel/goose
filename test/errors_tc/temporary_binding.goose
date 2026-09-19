// error: binding s to a slice rooted at a temporary, which does not outlive it
// A call's result lasts until the end of its statement, so a variable may not
// keep a view into it: here the next local would reuse its storage.
struct Txt { body: u8[] }
fn id(xs: Txt[:]) -> Txt[:] { xs }
fn mk() -> Txt[] { [Txt { body: "hey" }, Txt { body: "x" }] }
fn main() {
    let s = id(mk());
    let junk: u8[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    var n = 0;
    for t in s { n += t.body.len; }
    print(n, junk.len);
}
