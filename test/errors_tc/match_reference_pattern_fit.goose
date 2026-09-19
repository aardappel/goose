// error: match pattern does not fit the scrutinee type u8
// An integer match through a reference compares at the pointee's type, which
// bounds its patterns (§8.1).
fn main() {
    var n: u8 = 5;
    let r: u8& = n;
    print(match r { 0..10 => 1, 300 => 2, _ => 0 });
}
