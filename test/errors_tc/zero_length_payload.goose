// error: field v of variant Opt.Some cannot be a zero-length array: i64[0]
// A payload field is a field: a type argument making it a zero-length array
// is rejected where the enum is instantiated (§3.4).
enum Opt<T> { Some { v: T }, None }

fn main() {
    let o: Opt<i64[0]> = Opt<i64[0]>.Some { v: [] };
    match o { Some s => print(s.v.len), None => print("none") }
}
