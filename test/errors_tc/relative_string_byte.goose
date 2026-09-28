// error: a relative reference must point within the same root as its location
// error: this reference is rooted at static data
// A read-only u8 reference rooted at static data may point at a string
// literal's byte, not only be null, so it is no link within the array the
// slot is in (§3.9).
struct N { b: const u8&<u16>?, n: i64 }
fn main() {
    let lit = "abc";
    let r: const u8&? = lit[1];
    var ns: N[>..] = [];
    ns.push(N { b: r, n: 1 });
}
