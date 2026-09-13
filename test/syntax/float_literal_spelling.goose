// dump-runtime
// Float literals keep their spelling through a dump: an exponent form, a
// hex float and one that overflows to infinity all reparse as written,
// where a re-rendered value would lose the hex form and turn the
// infinity into text no lexer accepts.
fn main() {
    let a = 2.5e-3;
    let b = 0x1.8p3;
    let c = 1E5;
    let huge = 1e999;
    print(a, " ", b, " ", c, " ", huge > 1e308, " ", -huge < 0.0);
}
