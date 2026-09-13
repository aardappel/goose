// A plain assignment writes through a reference and leaves its nullness
// alone; a callee's local of the same name is not the loop's optional.
var n = 41;
var gcur: i64? = null;
fn helper() -> i64 { var cur = 0; cur = 5; return cur; }
fn ghelper() -> i64 { var gcur = 0; gcur = 5; return gcur; }
fn main() {
    var cur: i64? = n;
    assert(cur);
    for i in 2 { print(cur + helper()); }
    gcur .= n;
    for i in 2 { print(gcur + ghelper()); }
}
