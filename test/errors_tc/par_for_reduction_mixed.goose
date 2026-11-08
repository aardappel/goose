// error: t is a reduction of this par for by += and -=
// error: a reduction has one operation
// §4.6: one associative operation per reduction.
fn main() {
    var t = 0;
    par for k in 4 { t += k; t |= 1; }
    print(t);
}
