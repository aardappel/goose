// runtime-debug: constant casts must first round their source to f32.
let global: f32 = 16777217.0;

fn main() {
    let x: f32 = 16777217.0;
    print(x as! i64);
    print(x as i64);
    print(x as! u64);
    print(x as u64);
    print((x as f64) == 16777216.0);
    print((x as! f64) == 16777216.0);

    print(global as! i64);

    let negative: f32 = -16777217.0;
    print(negative as! i64);
    print(negative as i64);
    print((negative as f64) == -16777216.0);

    // This value is integral only after rounding to f32.
    let near_one: f32 = 0.99999999;
    print(near_one as i64);

    // A written f32 slot is a control for the constant folds above.
    var stored: f32 = 16777217.0;
    stored += 0.0;
    print(stored as! i64);
    print((stored as f64) == 16777216.0);

    // An f64 source must keep its extra precision.
    let wide: f64 = 16777217.0;
    print(wide as! i64);
    print(wide == 16777217.0);
}
