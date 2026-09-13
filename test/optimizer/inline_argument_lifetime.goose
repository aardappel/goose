// runtime-debug: inlining must keep temporary argument storage alive through
// the caller's statement, including views returned to another argument list.

fn make_a() -> u8[>..] {
    var result: u8[>..] = "abc";
    return result;
}

fn make_b() -> u8[>..] {
    var result: u8[>..] = "XYZ";
    return result;
}

fn consume(s: u8[:]) {
    var overwrite: u8[>..] = "XYZ";
    print(s);
    print(overwrite);
}

fn identity(s: u8[:]) -> u8[:] { s }

fn consume_pair(s: u8[:], other: u8[:]) {
    print(s);
    print(other);
}

fn inspect_image(b: u8[:]) {
    print(b[0]);
    var decoded, ok = from_bytes<u8[>..]>(b);
    print(ok, " ", b[0], " ", decoded[0]);
}

fn main() {
    consume(make_a()[..]);
    consume_pair(identity(make_a()[..]), make_b()[..]);
    var bytes: u8[>..] = [11, 22, 33];
    inspect_image(bytes.to_bytes());
}
