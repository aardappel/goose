// error: cannot pop arr: the for loop at
// error: iterates arr[1] in place, which may lie in arr
// A for over an element iterates it where it lies, at its fixed length:
// after the pop, the next iteration would read the popped slot, which the
// push refills (§5.2, §6.5).
fn main() {
    var arr: i64[3][>..<] = [[1, 2, 3], [4, 5, 6]];
    for x in arr[1] {
        arr.pop();
        arr.push([7, 7, 7]);
        print(x);
    }
}
