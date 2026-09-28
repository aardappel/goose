// error: cannot clear arr: the for loop at
// error: iterates arr[0].v in place, which may lie in arr
// A fixed-size field of an element lies in the array's storage too (§6.5).
struct S { n: i64, v: i64[3] }
fn main() {
    var arr: S[>..<] = [S { n: 0, v: [1, 2, 3] }];
    for x in arr[0].v {
        arr.clear();
        arr.push(S { n: 9, v: [7, 7, 7] });
        print(x);
    }
}
