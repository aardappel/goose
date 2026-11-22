// error: extern fn c_twice is the C symbol of export fn twice: call twice directly; only a C host may call an export (§7.11)
// The wrapper would run the export over the stacks of the Goose code that
// called it, whose tops that code may still hold in locals.
export "c_twice" fn twice(n: i64) -> i64 { n * 2 }
extern fn c_twice(n: i64) -> i64 = "c_twice";
fn main() { print(c_twice(2)); }
