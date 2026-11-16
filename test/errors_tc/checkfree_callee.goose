// error: checkfree fn sum_all calls twice, which calls get, which keeps a bounds check: nothing shows i < a.len
// What a checkfree function calls runs inside it, so it is judged as part of it. (docs/design/index_types.md)
var a: i32[>..] = [];
fn get(i: i64) -> i32 { a[i] }
fn twice(i: i64) -> i32 { get(i) + get(i) }
checkfree fn sum_all(n: i64) -> i64 {
    var t = 0;
    for i in n { t += twice(i); }
    return t;
}
fn main() {
    a.push(1);
    print(sum_all(a.len * 2));
}
