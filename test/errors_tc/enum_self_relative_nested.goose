// error: enum E has payloads holding self-relative references
// A self-relative reference anywhere in a payload's by-value parts counts,
// here in the elements of an array in a struct.
struct N { v: i64, r: N&<u8>? }
struct Pair { ns: N[2] }
enum E { Z, A { p: Pair } }
fn main() {
    var es: E[>..] = [];
    es.push(E.Z);
    print(es.len);
}
