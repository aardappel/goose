// error: cannot pop arr: the for loop at
// error: iterates r[1] in place, which may lie in arr
// r refers to the whole array, which a shrink leaves where it is, but the
// element the loop iterates through it lies in the part the pop frees
// (§5.2, §6.5).
fn main() {
    var arr: i64[3][>..<] = [[1, 2, 3], [4, 5, 6]];
    let r = arr;
    for x in r[1] {
        arr.pop();
        arr.push([7, 7, 7]);
        print(x);
    }
}
