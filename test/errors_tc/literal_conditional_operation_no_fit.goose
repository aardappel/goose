// error: the constants 0 to 300 it is computed from do not all fit u8
// §6.1, §6.4: an operation over a construct of constants adapts only to a
// type all its constants fit.

fn main() {
    var flags: bool[>..] = [true, false];
    let yes = flags[0];
    let z: u8 = (if yes {300} else {0}) + 1;
    print(z);
}
