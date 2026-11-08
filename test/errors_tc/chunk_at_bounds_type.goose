// error: expected a value of type const i64[:], got i32[3]
// chunk_at's bounds are i64 offsets: any i64 array or slice.
fn main() {
    let a:      i64[4] = [1, 2, 3, 4];
    let bounds: i32[3] = [0, 2, 4];
    print(a.chunk_at(bounds, 0));
}
