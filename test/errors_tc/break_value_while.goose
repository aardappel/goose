// break with a value exits loop/block only (§6.4).
fn main() {
    var i = 0;
    while i < 10 {
        i++;
        break 5;
    }
}
