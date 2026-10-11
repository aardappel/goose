// error: storing a reference rooted at text, which does not outlive the destination
import std;
struct Holder {text:const u8[:]}
fn apply<F>(text:const u8[:]) {F(text);}
fn relay<F>(text:const u8[:]) {apply(text,F);}
fn main() {
    var data:u8[>..]=[];data.append("kept");var holder=Holder {text:""};
    fn store(text:const u8[:]) {holder.text=text;}
    apply(data[..],store);relay(str(7),store);print(holder.text);
}
