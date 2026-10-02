// error: k is bound to a writable reference here, through which it may change, while the use as a literal at
// §4.4, §6.3: a nested function's body, checked once, adapts the loop's k to
// f32; the reference after it in every pass of the loop could change k.

fn scale(r: f64&) { r *= 1.0e300; }

fn main() {
    for i in 2 {
        let k = 0.5 + i;
        fn inner() -> f32 { k * 3 }
        print(inner());
        scale(k);
    }
}
