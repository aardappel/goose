// error: cannot pop arr: the for loop at
// error: iterates arr[0] in place, which may lie in arr
// The element a for loop iterates in place is live for its whole body, in a
// grow-only array as well (§5.1, §6.5).
fn main() {
    var arr: i64[3][>..] = [[1, 2, 3]];
    for x in arr[0] {
        arr.pop();
        arr.push([7, 7, 7]);
        print(x);
    }
}
