// error: expected a value of type i64, got f64 (a float converts to an integer only with an explicit `as`)
// An integer mixed with a float is a float (§6.1), which converts back to an
// integer only with an explicit `as` (§6.3).
fn main() {
    var integer: i64 = 1;
    var floating: f64 = 2.0;
    integer = integer + floating;
    print(integer);
}
