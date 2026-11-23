// Uninitialized fixed values above the native-value threshold still need a
// data stack when no expression names them, including nested locals.
struct Big { bytes: u8[6000], n: i64 }

fn plain() -> i64 {
    var buf: u8[8192];
    return 42;
}

fn record() -> i64 {
    var b: Big;
    return 7;
}

fn nested(c: bool) -> i64 {
    if c { var scratch: i64[1024]; }
    return 3;
}

fn used() -> i64 {
    var buf: u8[8192];
    buf[0] = 5;
    buf[8191] = 6;
    return buf[0] + buf[8191];
}

fn main() {
    print(plain());
    print(record());
    print(nested(true));
    print(used());
    print("large fixed uninit ok");
}
