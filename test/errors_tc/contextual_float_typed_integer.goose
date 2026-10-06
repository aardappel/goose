// error: no matching overload for call to min
// Context does not rescue an ordinary inference failure with a typed integer.
import std;
fn main() {
    var integer: i32 = 1;
    let value: f32 = min(integer, max(0.25, 0.5));
    print(value);
}
