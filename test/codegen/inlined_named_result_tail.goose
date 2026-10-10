// An inlined body's implicit return must land over its named early result.
struct Result { code: i32, out: u8[] }
fn run(reject: bool) -> Result {
    var result = Result { code: 1, out: "rejected" };
    if reject { return result; }
    var out: u8[>..] = "success";
    Result { code: 0, out: out }
}
fn command(reject: bool) -> Result { run(reject) }
fn array(reject: bool) -> u8[] {
    var result: u8[>..] = "early";
    if reject { return result; }
    "tail"
}
fn explicit(reject: bool) -> Result {
    var result = Result { code: 2, out: "early" };
    if reject { return result; }
    return Result { code: 3, out: "explicit" };
}
fn main() {
    for i in 2 {
        let reject = i == 1;
        let result = command(reject);
        assert(result.code == (if reject { 1 } else { 0 }));
        assert(result.out == (if reject { "rejected" } else { "success" }));
        print(result.code, " ", result.out);
        var receiver: u8[>..] = "prefix:";
        receiver.append(array(reject));
        assert(receiver == (if reject { "prefix:early" } else { "prefix:tail" }));
        print(receiver);
        let other = explicit(reject);
        assert(other.code == (if reject { 2 } else { 3 }));
        assert(other.out == (if reject { "early" } else { "explicit" }));
        print(other.code, " ", other.out);
    }
}
