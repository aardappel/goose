// error: storing a reference rooted at x, which does not outlive the destination
// The new elements land in the caller's storage, which a reference to a
// local of the callee does not outlive.
struct R { n: i64, refs: (i64&)[>..] }
fn fill(s: R&) {
    var x = 1;
    s.refs = [x];
}
fn main() {
    var y = 2;
    var r = R { n: 0, refs: [y] };
    fill(r);
    print(r.refs[0]);
}
