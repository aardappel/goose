// runtime-debug: a positive count whose element bytes exceed any data stack.
fn main() {
    var a: i64[>..] = [];
    print(a.check_length(9223372036854775807));
}
