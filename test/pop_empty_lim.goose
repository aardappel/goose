// Pop on an empty limited array of static capacity is a runtime abort
// (§9.3): the length is stored in the smallest int type fitting the
// capacity, so an unchecked pop would wrap it to that type's maximum and
// every later bounds check would pass against garbage.

fn main_pop_empty_lim() {
    var l: i64[..4] = [5];
    print(l.cap);                        // 4
    print(l.pop());                      // 5
    print(l.len);                        // 0
    print(l.pop());                      // Aborts.
}

fn main() { main_pop_empty_lim(); }
