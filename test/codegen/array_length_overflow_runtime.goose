fn make(n: i64) -> u8[] {
    var out: u8[>..] = [];
    for i in n { out.push(1); }
    return out;
}
fn main() { let bytes: u8[u8] = make(256); print(bytes.len); }
