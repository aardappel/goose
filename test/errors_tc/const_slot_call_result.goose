// error: storing a read-only slice of type u8[:] (read-only in this instantiation) in a slot of type u8[:]
// error: declare the slot const
// A result given a literal is read-only in that instantiation, so it lands
// only in a slot declared const (§9.5). The slot is the caller's: the
// callee's tail typechecks, and the rejection names the field initializer.
struct Named { name: u8[:] }
fn inner(s: u8[:]) -> u8[:] { s[1..s.len - 1] }
fn outer(s: u8[:]) -> u8[:] { inner(inner(s)) }
fn main() {
    let u = Named { name: outer("[[bob]]") };
    print(u.name);
}
