// from_bytes builds a grow-only array: its result is rooted at its own
// variable exactly as a pushed-into one is.
fn load(bytes: u8[:]) -> i64 {
    var a, ok = from_bytes<i64[]>(bytes);
    return if ok { a.len } else { -1 };
}

fn main() { print(load("")); }
