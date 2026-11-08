// error: converting to u32 in nodes checks against nodes.len, which reads nodes before it is assigned
// error: so nodes has no value until its initializer at
// A conversion into an index type reads its array's length (§3.11), so a
// function that first's initializer calls may not make one before nodes'
// initializer has run (§11.1), though nothing in it names nodes.
type Id = u32 in nodes;
fn convert<T>(n: i64) -> i64 {
    let v: i64 = n as T;
    v
}
var first = convert<Id>(0);
var nodes: i64[>..] = [10];
fn main() { print(first); }
