// error: no matching overload for call to size
// A [] takes its element type from its parameter's type (§4.2), which here
// names a type variable nothing binds.
fn size<T>(xs: const T[:]) -> i64 { xs.len }
fn main() {
    print(size([]));
}
