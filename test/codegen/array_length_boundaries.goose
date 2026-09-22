struct Packet { bytes: u8[u8], tail: i64 }
fn make(n: i64) -> u8[] {
    var out: u8[>..] = [];
    for i in n { out.push(1); }
    return out;
}
fn main() {
    let p = Packet { bytes: [1; 255], tail: 42 };
    let q: u8[u8] = make(255);
    let r: u8[u16] = make(256);
    print(p.bytes.len, " ", p.tail, " ", q.len, " ", r.len);
}
