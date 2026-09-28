// error: cannot pop arr: the for loop at
// error: iterates row(arr, 1), which may refer into arr
// The reference a call returns is read once, and the loop walks the element
// it refers to where it lies (§5.2, §6.5).
fn row(a: i64[3][>..<]&, i: i64) -> i64[3]& { a[i] }
fn main() {
    var arr: i64[3][>..<] = [[1, 2, 3], [4, 5, 6]];
    for x in row(arr, 1) {
        arr.pop();
        arr.push([7, 7, 7]);
        print(x);
    }
}
