// to_bytes refuses an element type holding a plain reference: an address
// means nothing outside the program that made it.
struct WithRef { v: i64, p: WithRef& }

fn main() {
    var a: WithRef[>..] = [];
    var img: u8[>..] = a.to_bytes();
    print(img.len);
}
