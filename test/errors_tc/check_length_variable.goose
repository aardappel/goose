// error: .check_length needs fixed-size elements
fn main() {
    var a: u8[][>..] = [];
    print(a.check_length(3));
}
