// runtime-debug: format_uleb and parse_uleb carry every i64, a negative one as the ten bytes of
// its 64 bits (§3.6: 1-10 bytes, full 64-bit range), and parse_uleb refuses what does not fit in
// 64 bits; valid input must not trip GS_DEBUG's `as` check inside std.
import std;

fn round_trip(v: i64, bytes: u8[:]) {
    var out: u8[>..] = [];
    format_uleb(out, v);
    let w, k = parse_uleb(out);
    print(out == bytes, " ", w, " ", k);
}

fn show(s: u8[:]) {
    let v, k = parse_uleb(s);
    print(v, " ", k);
}

fn main() {
    round_trip(0, [0x00]);                                                              // true 0 1
    round_trip(9223372036854775807, [0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f]);
                                                                                        // true 9223372036854775807 9
    round_trip(-1, [0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01]);       // true -1 10
    round_trip(-9223372036854775808, [0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01]);
                                                                                        // true -9223372036854775808 10
    // A tenth byte carries bit 63 at most, and there is no eleventh.
    show([0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01]);                 // -1 10
    show([0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x02]);                 // 0 0
    show([0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00]);           // 0 0
}
