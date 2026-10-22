// error: default<Holder>() does not exist: field never has no declared default and Never has no variants
// default<T>() is variant 0 of an ADT (§4.2), which an enum without
// variants has not got, here in a field.
enum Never {}
struct Holder { n: i64, never: Never }

fn main() {
    let h = default<Holder>();
    print(h.n);
}
