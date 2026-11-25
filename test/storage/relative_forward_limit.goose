// A u8-width self-relative slot holds signed offsets through +127.
struct Buffer { link: u8&<u8>?, bytes: u8[128] }

fn main() {
    var buf = Buffer { bytes: [0; 128] };
    buf.bytes[126] = 42;
    buf.link .= buf.bytes[126];     // Slot at byte 0, target at byte 127.
    let last = buf.link;
    assert(last);
    print(last);
    buf.link .= buf.bytes[127];     // Byte 128 is one past the positive limit.
}
