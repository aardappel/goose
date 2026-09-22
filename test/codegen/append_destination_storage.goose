// runtime-define: GS_MAX_STACKS=1
// The receiving array is the only data stack. Both an ordinary callee and
// its inlined form must build the named limited result there, then remove
// the packed header and unused slots. Rendering would need another stack.
fn make(n: i64) -> i64[..] {
    var result: i64[..] = [..n + 2];
    for i in n { result.push(i); }
    return result;
}
fn main() {
    var out: i64[>..] = [99];
    for k in 0..3 { out.append(if k == 0 { make(0) } else { make(k) }); }
    assert(out.len == 4);
    assert(out[0] == 99 && out[1] == 0 && out[2] == 0 && out[3] == 1);
}
