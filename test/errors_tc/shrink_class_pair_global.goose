// error: cannot call f: it shrinks gs while s is still used, and s may refer into gs
// f's parameter may be any array its callers pass, the global gs among them,
// so the two classes it gives g may be one array (§5.1).
var gs: i64[>..] = [1, 2, 3];
fn g(xs: i64[>..]&, s: i64[:]) { xs.pop(); xs.push(77); print(s[2]); }
fn f(a: i64[>..]&) { g(a, gs[..]); }
fn main() { f(gs); }
