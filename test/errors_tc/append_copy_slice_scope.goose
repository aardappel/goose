// error: storing a reference rooted at buf, which does not outlive the destination
// Elements appended out of a slice are read out of the storage it views
// (§9.5): the slices they hold point into buf, which ends first (§9.2).
fn main() {
    var words: u8[:][>..] = [];
    {
        var buf: u8[>..] = [];
        buf.append("ab cd");
        var parts: u8[:][>..] = [];
        parts.push(buf[0..2]);
        parts.push(buf[3..5]);
        words.append(parts[..]);
    }
    print(words.len);
}
