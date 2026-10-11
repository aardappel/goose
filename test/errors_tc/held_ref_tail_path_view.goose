// error: cannot call run, which shrinks b while b is still used: what it refers to may hold a reference or slice into it (§5.1)
// A reference to the struct's own tail, as a whole array, leads to the views
// taken of it: here one stored into a table the struct refers to, which go
// cannot tell from a view of another array (§5.1).
struct V { s: const u8[:] }
struct B { keys: u8[>..]&, vars: V&, r: u8[>..] }
fn run(b: B&) {
    b.r.clear();
    b.r.push(1);
}
fn go(b: B&) {
    b.vars.s = b.keys[0..1];
    for i in 3 { run(b); }
}
fn main() {
    var spare: u8[>..] = [1, 2];
    var v = V { s: "x" };
    var b = B { keys: spare, vars: v, r: [5, 6] };
    b.keys .= b.r;
    go(b);
    print(v.s);
}
