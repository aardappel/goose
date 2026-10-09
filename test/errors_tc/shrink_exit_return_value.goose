// error: cannot clear result while dict is still used
// The return after the shrink reads dict itself (§5.1).
fn g() -> i64 {
    var result: u8[>..] = [];
    var dict: (const u8[:])[>..] = [];
    result.push(1);
    if result.len > 0 {
        dict.push(result[0..1]);
        result.clear();
        return dict[0].len;
    }
    return 0;
}

fn main() {
    print(g());
}
