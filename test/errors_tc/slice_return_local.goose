// error: returning a reference rooted in t, which dies with this function
// A `-> const u8[:]` function's tail naming its own local array returns a
// view of it, not the array (§3.10, §9.2).
fn f() -> const u8[:] {
    var t: u8[>..] = [];
    t.push(65);
    t
}
fn main() { print(f()); }
