// A function value leaving a block or loop by a valued break still names its
// function, so a generic parameter binds to it (§7.6).
fn inc_fv(x: i64) -> i64 { x + 1 }
fn apply_fv<F>(x: i64) -> i64 { F(x) }
fn main() {
    print(apply_fv(4, block { break inc_fv; }));
    print(apply_fv(5, loop { break inc_fv; }));
}
