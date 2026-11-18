// error: cb is a variable, not a function
// A variable of the name hides the functions of it, as a call by that name
// would be hidden (§7.6, §11.1).
fn foo<inherited cb: fn()>() { cb(); }
fn main() {
    var cb = 5;
    foo();
}
