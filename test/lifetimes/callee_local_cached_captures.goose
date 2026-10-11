// runtime-debug
// Cached locals remain private while nested and function-value stores retain
// the ownership of the caller's holder.
import std;
struct Holder { text: const u8[:] }
fn nested(h:Holder&,text:const u8[:])->i64 {
    var r=Holder {text:text};
    fn write() {h.text=r.text;}
    write();
    return r.text.len;
}
fn apply<F>(text:const u8[:]) {F(text);}
fn captured(h:Holder&,text:const u8[:])->i64 {
    var r=Holder {text:text};
    apply(r.text) {s=>h.text=s;};
    return r.text.len;
}
fn deep(h:Holder&,text:const u8[:]) {
    for i in 0..3 {print(nested(h,text)," ",captured(h,text));}
}
fn main() {
    var data:u8[>..]=[];data.append("kept");
    var holder=Holder {text:""};
    print(nested(holder,data[..])," ",captured(holder,data[..]));
    deep(holder,data[..]);print(holder.text);
    fn store(text:const u8[:]) {holder.text=text;}
    store(data[..]);
    fn relay(text:const u8[:]) {store(text);}
    for i in 0..3 {relay(data[i..]);}
    print(holder.text);
    for i in 0..3 {apply(data[i..]) {s=>holder.text=s;};}
    print(holder.text);
}
