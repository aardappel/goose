fn main() {
    var xs: i64[>..<] = [1, 2];
    var out: u8[>..] = [];
    fn replace() -> u8[>..]& { xs.pop(); xs.push(9); return out; }
    to_bytes(xs, replace());
}
