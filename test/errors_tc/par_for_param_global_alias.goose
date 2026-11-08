// error: values is or may point into storage partitioned by this par for
// R2 (§4.4): dst stands for whatever its callers pass, which may be values,
// so a par for that owns dst's elements reads no global that may hold them.
var values: i64[>..] = [];

fn helper(dst: i64[:]) {
    par for k in dst.len { dst[k] = values[(k + 1) % dst.len] + 1; }
}

fn main() {
    for i in 100 { values.push(i); }
    helper(values);
    print(values[0]);
}
