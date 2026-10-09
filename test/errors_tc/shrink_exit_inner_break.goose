// error: cannot clear result while dict is in scope
// A break leaves only the innermost loop: the outer one runs on, and its
// next iteration stores into dict again (§5.1).
fn f(data: u8[:]) -> i64 {
    var result: u8[>..] = [];
    var dict: (const u8[:])[>..] = [];
    var pos = 0;
    while pos < data.len {
        while true {
            if data[pos] == 0 {
                result.clear();
                break;
            }
            break;
        }
        result.push(data[pos]);
        dict.push(result[0..1]);
        pos += 1;
    }
    return dict.len;
}

fn main() {
    let a: u8[3] = [65, 0, 67];
    print(f(a));
}
