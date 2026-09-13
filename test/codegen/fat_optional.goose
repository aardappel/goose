// An optional reference to a resizable value is a fat reference (C.2): its
// null test reads the header half of the pair.
fn main() {
    var s: u8[>..] = [];
    s.push('x');
    var o: u8[>..]? = null;
    if o == null { print("null"); }
    o .= s;
    if o != null { print("set ", o.len); }
    if o { print("truthy"); }
}
