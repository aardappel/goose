// error: cannot pop a: a[0], which the assignment writes after it
// error: may be in a
import vec;
fn main() {
    var a: float3[>..<] = [float3_1];
    a[0] += a.pop();
}
