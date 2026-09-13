// error: comparing u64 with i64 needs the signed side to be known non-negative (a literal, .len/.cap, or a `let` bound to one); convert it with `as` otherwise
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
