// warning: extern "fabs" fn is deprecated: write the symbol after the signature, extern fn old_abs(...) = "fabs";
// The symbol before `fn` still binds, as it did, and warns (spec §7.10);
// after the signature it is the current spelling.
extern "fabs" fn old_abs(x: f64) -> f64;
extern fn new_abs(x: f64) -> f64 = "fabs";
extern fn floor(x: f64 = 2.5) -> f64;
fn main() {
    print(old_abs(-1.5), " ", new_abs(-2.5), " ", floor());
}
