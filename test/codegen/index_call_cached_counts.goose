// A cast in a helper reads its array's count even without an identifier use.
var nodes: i32[>..] = [];
type Id = u32 in nodes;
fn convert(n: i64) -> Id { n as Id }
fn main() {
    nodes.push(10);
    nodes.push(20);
    print(convert(1));
}
