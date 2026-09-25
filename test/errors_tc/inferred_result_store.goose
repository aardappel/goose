// error: storing a reference rooted at c, which does not outlive the destination
// An inferred result that is a reference carries the root of what it refers
// to (§9.2): here the caller's local, too short-lived for a global.
struct Ctx { args: i64[>..<]& }
var keep: i64[>..<]? = null;
fn args(c: Ctx&) { c.args }
fn main() {
    var s: i64[>..<] = [1];
    var c = Ctx { args: s };
    keep .= args(c);
}
