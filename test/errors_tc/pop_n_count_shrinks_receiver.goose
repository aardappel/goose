// error: cannot pop outer: outer[0].lim, evaluated earlier in the statement, may still refer into outer
// pop_n's receiver is evaluated before its count (§2), so a count that
// shrinks the array the receiver lies in would leave it pointing at freed
// storage (§5.2).
struct H { lim: i64[..4] }
fn main() {
    var outer: H[>..<] = [];
    outer.push(H { lim: [1, 2, 3] });
    outer.push(H { lim: [4] });
    outer[0].lim.pop_n({ outer.pop(); 1 });
    print(outer.len);
}
