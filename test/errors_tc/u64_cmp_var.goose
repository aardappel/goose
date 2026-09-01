// §6.1: a `var` can be assigned anything later, so it does not inherit its
// initializer's non-negativity the way a `let` does.

fn main() {
    var a: i64[>..] = [];
    a.push(1);
    var n = a.len;
    n = 0 - 1;
    let h = 7 as u64;
    print(h < n);
}
