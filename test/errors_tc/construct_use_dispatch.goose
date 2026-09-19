// error: cannot call render in the value assigned to G: render uses G
// Any case function the dispatch may run counts (§8.2).
enum Shape { Circle { r: i64 }, Square { s: i64 } }
var G: u8[>..] = [];
fn render(c: Shape.Circle) -> u8[>..] { return [65]; }
fn render(q: Shape.Square) -> u8[>..] {
    var o: u8[>..] = [];
    for x in G { o.push(x); }
    return o;
}
fn main() {
    G.append("abc");
    let s: Shape = Shape.Circle { r: 1 };
    G = render(s);
    print(G);
}
