// A fixed result over CodeGen::FIXED_PTR_ABI_LIMIT is built straight into
// a fresh destination. Where an argument was lent (CodeGen::EmitArg) the
// result of a call into an existing variable goes through a temporary
// first (CodeGen::EmitSpecCall): the destination may be that variable,
// which the callee still reads. `recursive` keeps the callee a call at
// -O2. fixed_pointer_abi's mirror_self does not lend its variable: that
// one is also bound to a writable reference and captured by a block.
// Writing this result in place reads a field after the destination has
// overwritten it (§7.3).
import std;

struct Big { a: i64 = 0, b: i64 = 0, c: i64 = 0, rest: i64[40] = [0; 40] }

recursive fn mirror(d: Big) -> Big { Big { a: d.b, b: d.a, c: d.c + 1, .. } }

fn main() {
    var k = Big { a: 3, b: 4, c: 5, .. };
    k = mirror(k);
    print("mirror ", k.a, " ", k.b, " ", k.c);
}
