// A relative reference into a *field* of an element would need every valid
// address of its type enumerated; the verifier only knows element starts.
struct Deep { v: i64, link: i64&<u16>? }

fn load(bytes: u8[:]) -> i64 {
    var a, ok = from_bytes<Deep[>..]>(bytes);
    return if ok { a.len } else { -1 };
}

fn main() { print(load("")); }
