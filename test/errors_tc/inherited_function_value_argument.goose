// error: 1 function value(s) for 0 unbound generic parameter(s)
// No argument may carry the value, however: it is drawn by name at the call
// (§7.6), and nothing could bind it but one's scope.
fn foo<inherited cb: fn()>() { cb(); }
fn cb() { print("cb"); }
fn main() {
    foo(cb);
}
