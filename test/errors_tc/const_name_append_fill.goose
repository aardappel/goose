// error: array fill count K names a local variable, not a constant
// An appended literal's fill count is checked as any other's (§3.3, §4.2).
fn main() {
    let K = 5;
    var a: i64[>..] = [];
    a.append([1; K]);
    print(a.len);
}
