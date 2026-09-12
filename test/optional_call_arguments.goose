// A callee's rebinds take effect after the call's arguments are evaluated,
// for ordinary and case-function calls alike.
enum Choice { A, B }
var n = 41;
var g: i64? = null;
fn use(c: Choice.A, b: i64) { g .= null; print(b); }
fn use(c: Choice.B, b: i64) { print(b); }
fn plain(b: i64) { g .= null; print(b); }
fn main() {
    g .= n;
    var c: Choice = Choice.A {};
    use(c, g + 1);
    g .= n;
    plain(g + 1);
}
