// error: an index into i64[>..] can reach 2^48 - 1, which i32 does not hold
// An index binder's type holds every index its array can have (§6.5): only a
// fixed-size or static-capacity array's length bounds them below an i64's.
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    for x, i: i32 in xs {
        print(x, i);
    }
}
