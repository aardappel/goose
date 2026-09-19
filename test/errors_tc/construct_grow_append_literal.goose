// error: cannot call noisy, which grows ws: the run appended to ws is still under construction, and the growth would land inside it (§1.3)
// A literal of variable-size elements is built in place at the array's top,
// and one of its elements pushes into the same array (§1.3(4)).
fn noisy(ws: u8[][>..]&) -> u8[>..] { ws.push("x"); return str("n"); }
fn main() {
    var ws: u8[][>..] = [];
    ws.append([str("a"), noisy(ws)]);
    print(ws.len);
}
