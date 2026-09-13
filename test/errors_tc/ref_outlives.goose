// error: storing a reference rooted at local, which does not outlive the destination
// Rebinding a longer-lived reference to a shorter-lived target (§9.2).
struct Holder { r: i64& }
var gx = 1;
var gh = Holder { r: gx };
fn main() {
    var local = 2;
    gh.r .= local;
}
