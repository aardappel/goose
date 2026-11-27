// A u8-width self-relative slot holds signed offsets down to -128.
struct Buffer { bytes: u8[129], link: u8&<u8>? }

fn main() {
    var buf = Buffer { bytes: [0; 129] };
    buf.bytes[1] = 42;
    buf.link .= buf.bytes[1];       // Slot at byte 129, target at byte 1.
    let first = buf.link;
    assert(first);
    print(first);
    buf.link .= buf.bytes[0];       // -129 is outside the signed range.
}
