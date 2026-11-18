// runtime-debug: equality helper caches must retain reference origin through composites.
struct Group<T> { refs: T[1] }
struct Duo<T, U> { first: T, second: U }
struct BytesGroup<T> { next: T, refs: T[] }
enum Choice<T> { End, Link { next: T } }
struct VariableLinks { value: i64, group: BytesGroup<i64&<varint>?>, item: Choice<i64&<varint>?>.. }
struct SelfLinks {
    value: i64,
    refs: (i64&<u32>?)[1],
    nested: ((i64&<u32>?)[1])[1],
    limited: (i64&<u32>?)[..2],
    group: Group<i64&<u32>?>,
    reverse: (i64&<u16>?)[1],
    pair: Duo<i64&<u32>?, i64&<u32 in pool>?>,
    opposite: Duo<i64&<u32 in pool>?, i64&<u32>?>
}
var pool: i64[>..] = [7, 7];
struct PoolLinks {
    refs: (i64&<u32 in pool>?)[1],
    nested: ((i64&<u32 in pool>?)[1])[1],
    limited: (i64&<u32 in pool>?)[..2],
    group: Group<i64&<u32 in pool>?>,
    reverse: (i64&<u16 in pool>?)[1]
}
fn main() {
    var ns: SelfLinks[>..] = [];
    ns.push(SelfLinks { value: 7, refs: [null], nested: [[null]], limited: [], group: Group<i64&<u32>?> { refs: [null] }, reverse: [null], pair: Duo<i64&<u32>?, i64&<u32 in pool>?> { first: null, second: null }, opposite: Duo<i64&<u32 in pool>?, i64&<u32>?> { first: null, second: null } });
    ns.push(SelfLinks { value: 7, refs: [ns[0].value], nested: [[ns[0].value]], limited: [ns[0].value, null], group: Group<i64&<u32>?> { refs: [ns[0].value] }, reverse: [ns[0].value], pair: Duo<i64&<u32>?, i64&<u32 in pool>?> { first: ns[0].value, second: pool[0] }, opposite: Duo<i64&<u32 in pool>?, i64&<u32>?> { first: pool[0], second: ns[0].value } });
    ns.push(SelfLinks { value: 7, refs: [ns[0].value], nested: [[ns[0].value]], limited: [ns[0].value, null], group: Group<i64&<u32>?> { refs: [ns[0].value] }, reverse: [ns[0].value], pair: Duo<i64&<u32>?, i64&<u32 in pool>?> { first: ns[0].value, second: pool[0] }, opposite: Duo<i64&<u32 in pool>?, i64&<u32>?> { first: pool[0], second: ns[0].value } });
    ns.push(SelfLinks { value: 7, refs: [ns[2].value], nested: [[ns[2].value]], limited: [ns[2].value, null], group: Group<i64&<u32>?> { refs: [ns[2].value] }, reverse: [ns[2].value], pair: Duo<i64&<u32>?, i64&<u32 in pool>?> { first: ns[2].value, second: pool[0] }, opposite: Duo<i64&<u32 in pool>?, i64&<u32>?> { first: pool[0], second: ns[2].value } });
    let a = PoolLinks { refs: [pool[0]], nested: [[pool[0]]], limited: [pool[0], null], group: Group<i64&<u32 in pool>?> { refs: [pool[0]] }, reverse: [pool[0]] };
    let b = PoolLinks { refs: [pool[0]], nested: [[pool[0]]], limited: [pool[0], null], group: Group<i64&<u32 in pool>?> { refs: [pool[0]] }, reverse: [pool[0]] };
    let c = PoolLinks { refs: [pool[1]], nested: [[pool[1]]], limited: [pool[1], null], group: Group<i64&<u32 in pool>?> { refs: [pool[1]] }, reverse: [pool[1]] };
    print("self targets ", ns[1].refs[0] .== ns[2].refs[0], " ", ns[1].refs[0] .== ns[3].refs[0]);
    print("pool targets ", a.refs[0] .== b.refs[0], " ", a.refs[0] .== c.refs[0]);
    print("self arrays ", ns[1].refs == ns[2].refs, " ", ns[1].refs == ns[3].refs);
    print("pool arrays ", a.refs == b.refs, " ", a.refs == c.refs);
    print("self nested ", ns[1].nested == ns[2].nested, " ", ns[1].nested == ns[3].nested);
    print("pool nested ", a.nested == b.nested, " ", a.nested == c.nested);
    print("self limited ", ns[1].limited == ns[2].limited, " ", ns[1].limited == ns[3].limited);
    print("pool limited ", a.limited == b.limited, " ", a.limited == c.limited);
    print("self generic ", ns[1].group == ns[2].group, " ", ns[1].group == ns[3].group);
    print("pool generic ", a.group == b.group, " ", a.group == c.group);
    print("pool first ", a.reverse == b.reverse, " ", a.reverse == c.reverse);
    print("self second ", ns[1].reverse == ns[2].reverse, " ", ns[1].reverse == ns[3].reverse);
    print("self pool generic ", ns[1].pair == ns[2].pair, " ", ns[1].pair == ns[3].pair);
    print("pool self generic ", ns[1].opposite == ns[2].opposite, " ", ns[1].opposite == ns[3].opposite);
    var vs: VariableLinks[>..] = [];
    let v0 .= vs.push(VariableLinks { value: 7, group: BytesGroup<i64&<varint>?> { next: null, refs: [null] }, item: Choice<i64&<varint>?>.End {} });
    let v1 .= vs.push(VariableLinks { value: 7, group: BytesGroup<i64&<varint>?> { next: v0.value, refs: [v0.value] }, item: Choice<i64&<varint>?>.Link { next: v0.value } });
    let v2 .= vs.push(VariableLinks { value: 7, group: BytesGroup<i64&<varint>?> { next: v0.value, refs: [v0.value] }, item: Choice<i64&<varint>?>.Link { next: v0.value } });
    let v3 .= vs.push(VariableLinks { value: 7, group: BytesGroup<i64&<varint>?> { next: v2.value, refs: [v2.value] }, item: Choice<i64&<varint>?>.Link { next: v2.value } });
    let pa = BytesGroup<i64&<varint in pool>?> { next: pool[0], refs: [pool[0]] };
    let pb = BytesGroup<i64&<varint in pool>?> { next: pool[0], refs: [pool[0]] };
    let pc = BytesGroup<i64&<varint in pool>?> { next: pool[1], refs: [pool[1]] };
    let ea: Choice<i64&<varint in pool>?>.. = Choice<i64&<varint in pool>?>.Link { next: pool[0] };
    let eb: Choice<i64&<varint in pool>?>.. = Choice<i64&<varint in pool>?>.Link { next: pool[0] };
    let ec: Choice<i64&<varint in pool>?>.. = Choice<i64&<varint in pool>?>.Link { next: pool[1] };
    print("varint self targets ", v1.group.next .== v2.group.next, " ", v1.group.next .== v3.group.next);
    print("varint pool targets ", pa.next .== pb.next, " ", pa.next .== pc.next);
    print("varint self arrays ", v1.group.refs == v2.group.refs, " ", v1.group.refs == v3.group.refs);
    print("varint pool arrays ", pa.refs == pb.refs, " ", pa.refs == pc.refs);
    print("varint self generic ", v1.group == v2.group, " ", v1.group == v3.group);
    print("varint pool generic ", pa == pb, " ", pa == pc);
    print("self enums ", v1.item == v2.item, " ", v1.item == v3.item);
    print("pool enums ", ea == eb, " ", ea == ec);
    match v1.item {
        Link &x => match v2.item {
            Link &y => match v3.item {
                Link &z => print("self variants ", x == y, " ", x == z),
                End => {},
            },
            End => {},
        },
        End => {},
    }
    match ea {
        Link &x => match eb {
            Link &y => match ec {
                Link &z => print("pool variants ", x == y, " ", x == z),
                End => {},
            },
            End => {},
        },
        End => {},
    }
    print("controls ", ns[1].refs == ns[1].refs, " ", a.refs == a.refs, " ", ns[1].refs == ns[0].refs, " ", a.limited == b.limited, " ", 7 == 7, " ", 7 != 8);
}
