// error: an element cannot be a zero-length array: i64[0]
// A slice's elements are an array's, so a slice of zero-length arrays is
// rejected too (§3.4).
struct Rows { rows: i64[0][:] }

fn main() {
    let r = default<Rows>();
    print(r.rows.len);
}
