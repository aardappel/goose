// error: cannot clear bs: global gs may hold a reference into it, stored at
// use stores through the reference GH holds, which setup, checked after use,
// points at gs's slot: any global slice variable is what such a reference
// may name, whenever the reference to it is made (§3.8, §5.1).
struct H { r: u8[:]& }
var arr: u8[>..] = [];
var bs: u8[>..] = [];
var gs: u8[:] = arr[..];
var other: u8[:] = arr[..];
var GH = H { r: other };
fn use() { GH.r = bs[..]; }
fn setup() { GH.r .= gs; }
fn clearbs() {
    bs.clear();
    bs.append("QQQ");
}
fn main() {
    arr.append("ab");
    bs.append("xyz");
    use();
    setup();
    use();
    clearbs();
    print(gs);
}
