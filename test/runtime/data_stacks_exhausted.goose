// runtime-define: GS_MAX_STACKS=16
// Each activation holds str(n)'s temporary on a data stack of its own while
// the call below it runs (§7.8 allows that for unnamed temporaries), so
// deep enough recursion runs out of them: the abort names the declaration of
// the function that asked for one stack too many.
fn size(s: u8[:], k: i64) -> i64 { s.len + k }

recursive fn depth(n: i64) -> i64 {
    if n == 0 { return 0; }
    return size(str(n), depth(n - 1));
}

fn main() {
    print(depth(3));
    print(depth(100));
}
