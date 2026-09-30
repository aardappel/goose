// error: an array literal of variable-size elements is a u8[][], built only in a construction context
// A [] meeting a slice of variable-size elements is a T[] (§3.3, §4.2),
// which exists only in a construction context: viewing it takes a variable
// bound to it first, as it does a literal with elements.
fn count(ws: const u8[][:]) -> i64 { ws.len }
fn main() {
    print(count([]));
}
