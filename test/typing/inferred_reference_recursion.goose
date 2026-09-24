// A variable whose type is inferred from a reference to a non-fixed value is
// that reference (§3.8), not a non-fixed local built in place: bound inside
// a recursive cycle, it may stay in scope and in use across the calls back
// into the cycle (§7.8), as `let x .= r;` may. Each activation pushes
// through its own binding of the shared array before and after the call
// back, so the pushes nest. A binding initialized by a call into the cycle
// is a reference as well, in either function of a two-function cycle.
struct Ctx { args: i64[>..<]& }

recursive fn walk(c: Ctx&, n: i64) {
    let a = c.args;
    a.push(n);
    if n > 0 { walk(c, n - 1); }
    a.push(100 + n);
}

recursive fn outer(c: Ctx&, n: i64) -> i64[>..<]& {
    var a = inner(c, n);
    a.push(n * 10);
    return c.args;
}
fn inner(c: Ctx&, n: i64) -> i64[>..<]& {
    if n > 0 {
        let b = outer(c, n - 1);
        c.args.push(b.len);
    }
    return c.args;
}

fn main() {
    var s: i64[>..<] = [];
    var c = Ctx { args: s };
    walk(c, 2);
    print(s);                  // [2, 1, 0, 100, 101, 102]
    s.clear();
    let r = outer(c, 2);
    r.push(-1);
    print(s, " ", r.len);      // [0, 1, 10, 3, 20, -1] 6
}
