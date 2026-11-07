// error: in a par for
// R1 (§4.3): a reference read out of what the body sees from outside it is
// read-only there too, whatever its slot's type.
struct Box { r: i64& }

fn main() {
    var x = 1;
    let b = Box { r: &x };
    par for k in 2 { b.r = k; }
    print(x);
}
