// error: cannot pop c: the for loop at
// error: iterates c[..], which may refer into c
// A for loop walks a slice at the length it had when the loop began, not the
// array's: after the pops, the next iterations would read the slots the
// pushes refilled (§5.2, §6.5).
fn main() {
    var c: f64[>..<] = [1.0, 2.0, 3.0, 4.0];
    for x in c[..] {
        c.pop();
        c.pop();
        c.push(0.25);
        c.push(0.125);
        print(x);
    }
}
