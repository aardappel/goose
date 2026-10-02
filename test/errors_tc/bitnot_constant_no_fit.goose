// error: ~300 does not fit u8: 300 does not
// §6.1: `~c` adapts only to a type the constant c fits.

fn main() {
    let s: u8 = ~300;
    print(s);
}
