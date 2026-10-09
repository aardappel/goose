// error: cannot clear result while dict is in scope
// The break after the shrink does not end every path from it: the continue
// before it leads to the next iteration, which stores into dict again
// (§5.1).
fn f(data: u8[:]) -> i64 {
    var result: u8[>..] = [];
    var dict: (const u8[:])[>..] = [];
    var pos = 0;
    while pos < data.len {
        let start = result.len;
        pos += 1;
        if data[pos - 1] == 0 {
            result.clear();
            if pos < 2 { continue; }
            break;
        }
        result.push(data[pos - 1]);
        dict.push(result[start..start + 1]);
    }
    return dict.len;
}

fn main() {
    let a: u8[3] = [65, 0, 67];
    print(f(a));
}
