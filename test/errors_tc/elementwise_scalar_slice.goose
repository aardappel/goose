// error: operator * cannot be applied to i32[:] and i64

fn main() {
    var a: i32[2] = [1, 2]; let s = a[..]; print(s * 2);
}
