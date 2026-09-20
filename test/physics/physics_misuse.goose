// A call the program should not have made: a body used after it was
// destroyed. The physics layer reports it as it happens and skips it, and
// the next step aborts the program with the reason.
import std;
import vec;
import physics;

fn main() {
    let world = physics::create_world();
    let b = physics::create_body(world, physics::BodyDef { body_type: physics::DYNAMIC });
    physics::destroy(b);
    print("pushing a destroyed body");
    physics::set_linear_velocity(b, float3 { 1.0, 0.0, 0.0 });
    physics::step(world, 1.0 / 60.0, 4);
    print("not reached");
}
