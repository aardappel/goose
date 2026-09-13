// error: cannot call mk, which grows a: the run appended to a is still under construction, and the growth would land inside it (§1.3)
// A callee builds its result at the destination's top while also pushing
// into the destination array (§1.3(4)).
fn mk(a: i64[>..]&) -> i64[>..] {
    var r: i64[>..] = [];
    r.push(1);
    a.push(9);
    r.push(2);
    return r;
}
fn main() {
    var a: i64[>..] = [];
    a.append(mk(a));
    print(a);
}
