// error: from_bytes<N[>..<]> cannot hold relative references: a shrink would free what they still name
// The verifier checks each link against the loaded image, but a grow-shrink
// result can then free the element a link names while the array's own
// contents still hold it, and the store rule that keeps references into a
// T[>..<] out of storage (§5.2) never ran: the verified copy is not a store.
struct N { v: i64, next: N&<u32>? }

fn load(bytes: u8[:]) -> i64 {
    var a, ok = from_bytes<N[>..<]>(bytes);
    return if ok { a.len } else { -1 };
}

fn main() { print(load("")); }
