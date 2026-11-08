// error: cannot write through out in a par for: out is a variable of the enclosing function
// R2 (§4.4): an element the inner par for's iteration j owns is written by
// every iteration of the outer one, which sees out from outside its body.
fn main() {
    var out: i64[4] = [0, 0, 0, 0];
    par for i in 4 {
        par for j in 4 { out[j] = i; }
    }
    print(out);
}
