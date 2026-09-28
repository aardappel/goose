// runtime-define: GS_DEBUG=1
// -(i64.min) leaves i64 however the minimum is spelled: a signed overflow,
// which a debug build aborts on when it runs (§6.2), and which neither the
// checker nor the optimizer folds into a constant.
fn main() {
    print(-9223372036854775808);
    print(-(-9223372036854775808));
    print("unreachable");
}
