// error: a relative reference in bytes must point into bytes
// error: this reference is rooted at static data
// Static data holds string literals as well as the null an optional may be:
// a read-only u8 reference rooted there may point at a literal's byte, which
// no link into bytes encodes (§3.9).
var bytes: u8[>..] = [];
struct B { b: const u8&<u16 in bytes>? }
fn main() {
    bytes.push(7);
    let lit = "abc";
    let r: const u8&? = lit[0];
    let x = B { b: r };
}
