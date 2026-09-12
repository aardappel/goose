// A back edge checked before the `return ... from` it reaches still has to
// agree on the target specialization: the innermost g here is g<i32>.
fn g<T>(x: T, n: i64) -> T {
    walk(n);
    print("g normal exit ", n);
    return x;
}
recursive fn walk(n: i64) {
    if n > 0 {
        let z: i32 = 7;
        let r = g(z, n - 1);
        print("inner g returned ", r);
        return;
    }
    return 42 from g;
}
fn main() {
    let v: i64 = 1;
    print("outer g returned ", g(v, 1));
}
