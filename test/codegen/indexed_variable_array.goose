// Random access into variable-sized records through an index of
// pool-relative references, including after the records pool grows.

struct Record {
    name: u8[],
    data: u8[],
}

var records: Record[>..] = [];
var index: Record&<u32 in records>[>..] = [];

fn main() {
    let a = records.push(Record { name: "alpha", data: "first payload" });
    let b = records.push(Record { name: "bravo", data: "second payload" });
    let c = records.push(Record { name: "charlie", data: "third payload" });

    index.push(a);
    index.push(b);
    index.push(c);

    // Grow the records pool after creating the index.
    for i in 100 {
        records.push(Record { name: "extra", data: "growth test" });
    }

    // Check the pool and index lengths.
    assert(records.len == 103);
    assert(index.len == 3);

    // Check that the original references still reach the right records.
    assert(index[0].name == "alpha");
    assert(index[0].data == "first payload");
    assert(index[1].name == "bravo");
    assert(index[1].data == "second payload");
    assert(index[2].name == "charlie");
    assert(index[2].data == "third payload");

    print("indexed variable array: ok");
}
