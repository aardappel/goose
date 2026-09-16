// A `return` moves the function's own non-fixed locals (§4.1) in whichever
// position they take among the values: a call checked earlier in the same
// return list, and the returns inside its body, leave this return's context
// in force for the values that follow it.
fn side() -> i64 { var q: i64[>..] = [7, 8]; return q.len; }
fn make() -> i64, i64[>..] {
    var b: i64[>..] = [1, 2, 3];
    return side(), b;
}
fn main() {
    let n, xs = make();
    print(n);
    print(xs);
}
