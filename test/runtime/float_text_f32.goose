// runtime-debug: an f32's text is the shortest that reads back as the same
// f32 (§3.7), laid out as f64 text is: print, str, format, format hooks,
// and f32s inside structs, arrays, ADTs, optionals and generic types.
// Every value comes from an array at run time, so no C compiler folds it.
import vec;

struct Hit { at: f64, w: f32 }
enum Reading { Missing, Value { v: f32 } }
struct Wrapped { v: f32 }
fn format(out: u8[>..]&, w: const Wrapped&) { format(out, "<", w.v, ">"); }
struct Pair<T> { a: T, b: T }

fn main() {
    var src: f32[>..] = [0.1, 1.0, 3.0, 0.0, 1e-7, 3.4e38, 16777216.0, 1e6, 1.0000001,
                         1e-45, 123456.7, 0.3, 2.5e-10, 1e15, 1e16, 1e17];
    let tenth = src[0];
    let third = src[1] / src[2];
    let nz = -src[3];
    print(tenth, " ", third, " ", nz, " ", src[1], " ", -tenth);
    print(src[4], " ", src[5], " ", src[6], " ", src[6] + src[1], " ", src[7]);
    print(src[8], " ", src[9], " ", src[10], " ", src[11], " ", src[12]);
    print(src[13], " ", src[14], " ", src[15], " ", src[15] * src[7] * src[7]);
    // The f64 an f32 widens to keeps its own digits.
    print(tenth as f64, " ", third as f64, " ", src[8] as f64);

    print(str(tenth, ",", third, ",", src[10]));
    var text: u8[>..] = [];
    format(text, tenth, " ", third * src[2], " ", src[11]);
    print(text);

    print([tenth, third, src[11]], " ", src[0..3]);
    print(Hit { tenth, tenth }, " ", Reading.Value { third });
    let maybe: f32? = src[11];
    print(maybe, " ", Wrapped { tenth }, " ", [Wrapped { third }]);
    print(vec3<f32> { tenth, third, src[11] }, " ", Pair<f32> { tenth, src[12] },
          " ", Pair<f64> { tenth, third });
}
