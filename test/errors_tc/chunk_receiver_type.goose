// error: .chunk_at is not available on i64
// The receiver of chunk_at is an array or a slice, in either spelling.
fn main() {
    let x = 5;
    let bounds: i64[2] = [0, 1];
    print(chunk_at(x, bounds, 0));
}
