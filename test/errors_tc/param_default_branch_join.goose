// error: branches have mismatched types: i64[3] vs i64[2]
// A default joins arrays of two types as a slice only at a parameter
// declared a slice, as an argument's branches do (§6.4, §7.1).
var gflag = true;
var ga3: i64[3] = [1, 2, 3];
var ga2: i64[2] = [10, 20];

fn first3(x: i64[3] = if gflag { ga3 } else { ga2 }) -> i64 { x[0] }

fn main() {
    print(first3());
}
