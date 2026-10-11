// error: storing a reference rooted at text, which does not outlive the destination
import std;
struct Holder {text:const u8[:]}
fn main() {
    var data:u8[>..]=[];data.append("kept");var holder=Holder {text:""};
    fn store(text:const u8[:]) {var local=Holder {text:text};holder.text=local.text;}
    store(data[..]);
    fn deep() {store(str(7));}
    deep();print(holder.text);
}
