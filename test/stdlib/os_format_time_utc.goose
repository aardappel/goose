import os;

fn show(seconds: i64) {
    var out: u8[>..] = [];
    out.append("[");
    let ok = format_time_utc(out, seconds);
    print(ok, " ", out, "]");
}

fn main() {
    show(0);
    show(-1);
    show(-86400);
    show(-86401);
    show(951782400);       // 2000 is a leap year.
    show(4107542400);      // 2100 is not a leap year.
    show(-62135596800);
    show(253402300799);
    show(-62135596801);
    show(253402300800);
    show(-9223372036854775808);
    show(9223372036854775807);
}
