// runtime-debug: an integer's text (§3.7), through print, str, format into
// a resizable and into a limited array, and std's format_int and
// format_uint: every length from 1 to 20 digits, both ends of each width,
// and padding. format_int takes i64.min's magnitude without overflowing.
import std;

fn main() {
    // Read from arrays, so no value is known to the optimizer.
    var ints: i64[>..] = [0, 7, -7, 10, -10, 99, -100, 4294967296, -999999999999999999,
                          9223372036854775807, -9223372036854775807, -9223372036854775808];
    var wides: u64[>..] = [0, 9999999999999999999, 10000000000000000000, 18446744073709551615];
    var halves: i32[>..] = [-2147483648, 2147483647];
    var bytes: u8[>..] = [0, 255];
    var shorts: i16[>..] = [-32768];

    // Each power of ten and its neighbours, as u64 and as negative i64.
    var p: u64 = wides[0] + 1;
    for k in 20 {
        var line: u8[>..] = [];
        format(line, p - 1, " ", p, " ", p + 1);
        if k < 19 { format(line, " ", -(p as! i64), " ", 1 - (p as! i64)); }
        print(line);
        p *= 10;
    }

    for v in ints { print(v); }
    for v in wides { print(v); }
    print(halves[0], " ", halves[1], " ", bytes[0], " ", bytes[1], " ", shorts[0]);

    let s = str(ints[11], "/", wides[3]);
    print(s.len, " ", s);

    var lim: u8[..48] = [];
    format(lim, "[", ints[11], "|", wides[3], "]");
    print(lim.len, " ", lim);

    var o: u8[>..] = [];
    format_int(o, ints[11], 10, 0, ' ');
    o.push('|');
    format_int(o, ints[11], 10, 25, '0');
    o.push('|');
    format_int(o, ints[9], 10, 0, ' ');
    o.push('|');
    format_int(o, ints[2], 10, 4, ' ');
    o.push('|');
    format_int(o, ints[0], 10, 3, '0');
    o.push('|');
    format_uint(o, wides[3], 10, 21, '*');
    o.push('|');
    format_uint(o, wides[3], 16, 0, ' ');
    o.push('|');
    format_int(o, ints[11], 16, 0, ' ');
    o.push('|');
    format_int(o, ints[2], 2, 6, '0');
    print(o);
}
