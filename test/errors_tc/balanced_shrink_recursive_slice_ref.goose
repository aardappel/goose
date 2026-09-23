// error: cannot call walk, which shrinks scratch while held
// error: is still used: the slice it refers to may point into scratch
// A reference to the slice variable holding an activation's view keeps the
// view in use across the call back into the cycle (§5.2); the pop after the
// resize makes walk unbalanced, so the scan the call back skipped reports it
// once the cycle is checked.
recursive fn walk(scratch: i64[>..<]&, depth: i64) -> i64 {
    let mark = scratch.len;
    scratch.push(depth);
    var mine = scratch[mark..];
    let held = &mine;
    var total = 0;
    if depth < 3 { total = walk(scratch, depth + 1); }
    total += held[0];
    scratch.resize(mark);
    if depth == 3 { scratch.pop(); }
    return total;
}
fn main() {
    var scratch: i64[>..<] = [8];
    print(walk(scratch, 1));
}
