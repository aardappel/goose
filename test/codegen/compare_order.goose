// An equality reads its left operand before its right operand runs, even
// when that rebinds the reference naming the left one.
struct Named { id: i64, name: u8[] }
fn main() {
    let s1: u8[] = "ab";
    let s2: u8[] = "cd";
    var r .= s1;
    fn change() -> u8[] { r .= s2; return "ab"; }
    print(r == change());
    let n1 = Named { id: 1, name: "x" };
    let n2 = Named { id: 2, name: "y" };
    var nr .= n1;
    fn change_named() -> Named { nr .= n2; return Named { id: 1, name: "x" }; }
    print(nr == change_named());
    var a: i64[>..] = [1, 2];
    var b: i64[>..] = [3, 4];
    var q .= a;
    fn change_array() -> i64[>..] { q .= b; return [1, 2]; }
    print(q == change_array());
}
