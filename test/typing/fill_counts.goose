// Constant fill counts must be lowered before optimizer, BCE and codegen.
let COUNT = 2;
struct Packet { values: i64[3] }

fn repeated() -> i64[2] { [4; COUNT] }

fn main() {
    let named = [7; COUNT];
    let arithmetic = [8; 1 + 1];
    let annotated: i64[3] = [9; COUNT + 1];
    let nested = Packet { [10; COUNT + 1] };
    var out: i64[>..] = [];
    out.append([11; COUNT]);
    print(named, arithmetic, annotated, nested, repeated(), out);
}
