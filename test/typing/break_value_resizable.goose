// A valued break constructs into the type its construct is expected to have,
// as the construct's tail value does.
fn main() {
    var xs: i64[>..<] = [1, 2];
    xs = block { break [7, 8, 9]; };
    print(xs);
    var zs: i64[>..] = [1];
    zs = loop { break [4, 5]; };
    zs.push(6);
    print(zs);
    var a: i64[>..] = block { break []; };
    a.push(3);
    print(a);
    let small: u8 = block { if zs.len > 0 { break 7; } 9 };
    print(small);
}
