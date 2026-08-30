// All break E of one construct must agree on E's type (§6.4).
fn main() {
    let x = loop {
        let c = true;
        if c { break 1; }
        break 2.5;
    };
    print(x);
}
