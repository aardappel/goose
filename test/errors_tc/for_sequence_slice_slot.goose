// error: cannot pop arr: the for loop at
// error: reads arr[0].items again on every iteration, which may lie in arr
// A slice read out of an element: the loop loads its pointer and length
// again on every iteration, out of storage the pop gives back (§5.2, §6.5).
struct S { items: const u8[:] }
fn main() {
    var arr: S[>..<] = [S { items: "abc" }];
    for c in arr[0].items {
        arr.pop();
        print(c);
    }
}
