/* Bodies: creation from a Goose-shaped definition, their state, forces,
   mass, settings, what is attached to them, and queries against one body. */

#include "physics_internal.h"

static gs_phys_float3 phys_zero3(void) {
    gs_phys_float3 r = { 0, 0, 0 };
    return r;
}

static gs_phys_transform phys_identity(void) {
    gs_phys_transform r = { { 0, 0, 0 }, { 0, 0, 0, 1 } };
    return r;
}

static bool phys_body_type_ok(int64_t type, const char *fn) {
    if (type >= b3_staticBody && type <= b3_dynamicBody) return true;
    return phys_misuse("physics::%s: no body type %lld (STATIC, KINEMATIC or DYNAMIC)", fn,
                       (long long)type);
}

gs_phys_body gs_phys_create_body(gs_phys_world w, gs_phys_body_def def) {
    gs_phys_body none = { 0 };
    PHYS_WORLD(w, "create_body", none);
    if (!phys_body_type_ok(def.body_type, "create_body")) return none;
    b3BodyDef d = b3DefaultBodyDef();
    d.type = (b3BodyType)def.body_type;
    d.position = b3v3(def.position);
    d.rotation = b3q(def.rotation);
    d.linearVelocity = b3v3(def.linear_velocity);
    d.angularVelocity = b3v3(def.angular_velocity);
    d.linearDamping = def.linear_damping;
    d.angularDamping = def.angular_damping;
    d.gravityScale = def.gravity_scale;
    d.sleepThreshold = def.sleep_threshold;
    d.safetyFactor = def.safety_factor;
    d.motionLocks.linearX = def.motion_locks.linear_x;
    d.motionLocks.linearY = def.motion_locks.linear_y;
    d.motionLocks.linearZ = def.motion_locks.linear_z;
    d.motionLocks.angularX = def.motion_locks.angular_x;
    d.motionLocks.angularY = def.motion_locks.angular_y;
    d.motionLocks.angularZ = def.motion_locks.angular_z;
    d.enableSleep = def.enable_sleep;
    d.isAwake = def.is_awake;
    d.isBullet = def.is_bullet;
    d.isEnabled = def.is_enabled;
    d.allowFastRotation = def.allow_fast_rotation;
    d.enableContactRecycling = def.enable_contact_recycling;
    d.userData = (void *)(uintptr_t)def.user_data;
    return phys_body_of(b3CreateBody(id, &d));
}

void gs_phys_body_destroy(gs_phys_body b) {
    PHYS_BODY(b, "destroy", );
    b3DestroyBody(id);
    phys_sweep();
}

uint8_t gs_phys_body_is_valid(gs_phys_body b) {
    return b.id && b3Body_IsValid(b3LoadBodyId(b.id));
}

int32_t gs_phys_body_type(gs_phys_body b) {
    PHYS_BODY(b, "body_type", 0);
    return b3Body_GetType(id);
}

void gs_phys_body_set_type(gs_phys_body b, int64_t body_type) {
    PHYS_BODY(b, "set_type", );
    if (phys_body_type_ok(body_type, "set_type")) b3Body_SetType(id, (b3BodyType)body_type);
}

void gs_phys_body_set_name(gs_phys_body b, gs_phys_bytes name) {
    PHYS_BODY(b, "set_name", );
    b3Body_SetName(id, phys_cstr(name));
}

int64_t gs_phys_body_name(gs_phys_body b, gs_phys_bytes out) {
    PHYS_BODY(b, "name", 0);
    return phys_copy_text(b3Body_GetName(id), out);
}

void gs_phys_body_set_user_data(gs_phys_body b, uint64_t data) {
    PHYS_BODY(b, "set_user_data", );
    b3Body_SetUserData(id, (void *)(uintptr_t)data);
}

uint64_t gs_phys_body_user_data(gs_phys_body b) {
    PHYS_BODY(b, "user_data", 0);
    return (uint64_t)(uintptr_t)b3Body_GetUserData(id);
}

gs_phys_float3 gs_phys_body_position(gs_phys_body b) {
    PHYS_BODY(b, "position", phys_zero3());
    return gsf3(b3Body_GetPosition(id));
}

gs_phys_quat gs_phys_body_rotation(gs_phys_body b) {
    gs_phys_quat identity = { 0, 0, 0, 1 };
    PHYS_BODY(b, "rotation", identity);
    return gsq(b3Body_GetRotation(id));
}

gs_phys_transform gs_phys_body_transform(gs_phys_body b) {
    PHYS_BODY(b, "transform", phys_identity());
    return gsxf(b3Body_GetTransform(id));
}

void gs_phys_body_transforms(gs_phys_body_slice bodies, gs_phys_transform_slice out) {
    if (bodies.len != out.len) {
        phys_misuse("physics::transforms: %lld bodies into %lld transforms", (long long)bodies.len,
                    (long long)out.len);
        return;
    }
    bool reported = false;
    for (int64_t i = 0; i < bodies.len; i++) {
        b3BodyId id = b3LoadBodyId(bodies.data[i].id);
        if (bodies.data[i].id && b3Body_IsValid(id)) {
            out.data[i] = gsxf(b3Body_GetTransform(id));
        } else {
            out.data[i] = phys_identity();
            if (!reported) {
                phys_misuse("physics::transforms: body %lld was destroyed, or never created",
                            (long long)i);
                reported = true;
            }
        }
    }
}

void gs_phys_body_set_transform(gs_phys_body b, gs_phys_float3 position, gs_phys_quat rotation) {
    PHYS_BODY(b, "set_transform", );
    b3Body_SetTransform(id, b3v3(position), b3q(rotation));
}

gs_phys_float3 gs_phys_body_local_point(gs_phys_body b, gs_phys_float3 world_point) {
    PHYS_BODY(b, "local_point", phys_zero3());
    return gsf3(b3Body_GetLocalPoint(id, b3v3(world_point)));
}

gs_phys_float3 gs_phys_body_world_point(gs_phys_body b, gs_phys_float3 local_point) {
    PHYS_BODY(b, "world_point", phys_zero3());
    return gsf3(b3Body_GetWorldPoint(id, b3v3(local_point)));
}

gs_phys_float3 gs_phys_body_local_vector(gs_phys_body b, gs_phys_float3 world_vector) {
    PHYS_BODY(b, "local_vector", phys_zero3());
    return gsf3(b3Body_GetLocalVector(id, b3v3(world_vector)));
}

gs_phys_float3 gs_phys_body_world_vector(gs_phys_body b, gs_phys_float3 local_vector) {
    PHYS_BODY(b, "world_vector", phys_zero3());
    return gsf3(b3Body_GetWorldVector(id, b3v3(local_vector)));
}

gs_phys_float3 gs_phys_body_linear_velocity(gs_phys_body b) {
    PHYS_BODY(b, "linear_velocity", phys_zero3());
    return gsf3(b3Body_GetLinearVelocity(id));
}

gs_phys_float3 gs_phys_body_angular_velocity(gs_phys_body b) {
    PHYS_BODY(b, "angular_velocity", phys_zero3());
    return gsf3(b3Body_GetAngularVelocity(id));
}

void gs_phys_body_set_linear_velocity(gs_phys_body b, gs_phys_float3 velocity) {
    PHYS_BODY(b, "set_linear_velocity", );
    b3Body_SetLinearVelocity(id, b3v3(velocity));
}

void gs_phys_body_set_angular_velocity(gs_phys_body b, gs_phys_float3 velocity) {
    PHYS_BODY(b, "set_angular_velocity", );
    b3Body_SetAngularVelocity(id, b3v3(velocity));
}

void gs_phys_body_set_target_transform(gs_phys_body b, gs_phys_transform target, float time_step,
                                       uint8_t wake) {
    PHYS_BODY(b, "set_target_transform", );
    if (!(time_step > 0.0f)) {
        phys_misuse("physics::set_target_transform: a time step of %g", (double)time_step);
        return;
    }
    b3Body_SetTargetTransform(id, b3xf(target), time_step, wake);
}

gs_phys_float3 gs_phys_body_local_point_velocity(gs_phys_body b, gs_phys_float3 local_point) {
    PHYS_BODY(b, "local_point_velocity", phys_zero3());
    return gsf3(b3Body_GetLocalPointVelocity(id, b3v3(local_point)));
}

gs_phys_float3 gs_phys_body_world_point_velocity(gs_phys_body b, gs_phys_float3 world_point) {
    PHYS_BODY(b, "world_point_velocity", phys_zero3());
    return gsf3(b3Body_GetWorldPointVelocity(id, b3v3(world_point)));
}

void gs_phys_body_apply_force(gs_phys_body b, gs_phys_float3 force, gs_phys_float3 point,
                              uint8_t wake) {
    PHYS_BODY(b, "apply_force", );
    b3Body_ApplyForce(id, b3v3(force), b3v3(point), wake);
}

void gs_phys_body_apply_force_to_center(gs_phys_body b, gs_phys_float3 force, uint8_t wake) {
    PHYS_BODY(b, "apply_force_to_center", );
    b3Body_ApplyForceToCenter(id, b3v3(force), wake);
}

void gs_phys_body_apply_torque(gs_phys_body b, gs_phys_float3 torque, uint8_t wake) {
    PHYS_BODY(b, "apply_torque", );
    b3Body_ApplyTorque(id, b3v3(torque), wake);
}

void gs_phys_body_apply_linear_impulse(gs_phys_body b, gs_phys_float3 impulse, gs_phys_float3 point,
                                       uint8_t wake) {
    PHYS_BODY(b, "apply_linear_impulse", );
    b3Body_ApplyLinearImpulse(id, b3v3(impulse), b3v3(point), wake);
}

void gs_phys_body_apply_linear_impulse_to_center(gs_phys_body b, gs_phys_float3 impulse,
                                                 uint8_t wake) {
    PHYS_BODY(b, "apply_linear_impulse_to_center", );
    b3Body_ApplyLinearImpulseToCenter(id, b3v3(impulse), wake);
}

void gs_phys_body_apply_angular_impulse(gs_phys_body b, gs_phys_float3 impulse, uint8_t wake) {
    PHYS_BODY(b, "apply_angular_impulse", );
    b3Body_ApplyAngularImpulse(id, b3v3(impulse), wake);
}

float gs_phys_body_mass(gs_phys_body b) {
    PHYS_BODY(b, "mass", 0.0f);
    return b3Body_GetMass(id);
}

gs_phys_mat3 gs_phys_body_rotational_inertia(gs_phys_body b) {
    gs_phys_mat3 none;
    memset(&none, 0, sizeof none);
    PHYS_BODY(b, "rotational_inertia", none);
    return gsm3(b3Body_GetLocalRotationalInertia(id));
}

float gs_phys_body_inverse_mass(gs_phys_body b) {
    PHYS_BODY(b, "inverse_mass", 0.0f);
    return b3Body_GetInverseMass(id);
}

gs_phys_mat3 gs_phys_body_world_inverse_inertia(gs_phys_body b) {
    gs_phys_mat3 none;
    memset(&none, 0, sizeof none);
    PHYS_BODY(b, "world_inverse_inertia", none);
    return gsm3(b3Body_GetWorldInverseRotationalInertia(id));
}

gs_phys_float3 gs_phys_body_local_center(gs_phys_body b) {
    PHYS_BODY(b, "local_center", phys_zero3());
    return gsf3(b3Body_GetLocalCenter(id));
}

gs_phys_float3 gs_phys_body_world_center(gs_phys_body b) {
    PHYS_BODY(b, "world_center", phys_zero3());
    return gsf3(b3Body_GetWorldCenter(id));
}

void gs_phys_body_set_mass_data(gs_phys_body b, gs_phys_mass_data data) {
    PHYS_BODY(b, "set_mass_data", );
    if (b3Body_GetType(id) != b3_dynamicBody) {
        phys_misuse("physics::set_mass_data: only a dynamic body has a mass");
        return;
    }
    if (!(data.mass > 0.0f) || data.mass > FLT_MAX) {
        phys_misuse("physics::set_mass_data: a mass of %g", (double)data.mass);
        return;
    }
    b3Body_SetMassData(id, b3mass(data));
}

gs_phys_mass_data gs_phys_body_mass_data(gs_phys_body b) {
    gs_phys_mass_data none;
    memset(&none, 0, sizeof none);
    PHYS_BODY(b, "mass_data", none);
    return gsmass(b3Body_GetMassData(id));
}

void gs_phys_body_apply_mass_from_shapes(gs_phys_body b) {
    PHYS_BODY(b, "apply_mass_from_shapes", );
    b3Body_ApplyMassFromShapes(id);
}

void gs_phys_body_set_linear_damping(gs_phys_body b, float damping) {
    PHYS_BODY(b, "set_linear_damping", );
    b3Body_SetLinearDamping(id, damping);
}

float gs_phys_body_linear_damping(gs_phys_body b) {
    PHYS_BODY(b, "linear_damping", 0.0f);
    return b3Body_GetLinearDamping(id);
}

void gs_phys_body_set_angular_damping(gs_phys_body b, float damping) {
    PHYS_BODY(b, "set_angular_damping", );
    b3Body_SetAngularDamping(id, damping);
}

float gs_phys_body_angular_damping(gs_phys_body b) {
    PHYS_BODY(b, "angular_damping", 0.0f);
    return b3Body_GetAngularDamping(id);
}

void gs_phys_body_set_gravity_scale(gs_phys_body b, float scale) {
    PHYS_BODY(b, "set_gravity_scale", );
    b3Body_SetGravityScale(id, scale);
}

float gs_phys_body_gravity_scale(gs_phys_body b) {
    PHYS_BODY(b, "gravity_scale", 0.0f);
    return b3Body_GetGravityScale(id);
}

uint8_t gs_phys_body_is_awake(gs_phys_body b) {
    PHYS_BODY(b, "is_awake", 0);
    return b3Body_IsAwake(id);
}

void gs_phys_body_set_awake(gs_phys_body b, uint8_t awake) {
    PHYS_BODY(b, "set_awake", );
    b3Body_SetAwake(id, awake);
}

void gs_phys_body_enable_sleep(gs_phys_body b, uint8_t flag) {
    PHYS_BODY(b, "enable_sleep", );
    b3Body_EnableSleep(id, flag);
}

uint8_t gs_phys_body_sleep_enabled(gs_phys_body b) {
    PHYS_BODY(b, "sleep_enabled", 0);
    return b3Body_IsSleepEnabled(id);
}

void gs_phys_body_set_sleep_threshold(gs_phys_body b, float threshold) {
    PHYS_BODY(b, "set_sleep_threshold", );
    b3Body_SetSleepThreshold(id, threshold);
}

float gs_phys_body_sleep_threshold(gs_phys_body b) {
    PHYS_BODY(b, "sleep_threshold", 0.0f);
    return b3Body_GetSleepThreshold(id);
}

void gs_phys_body_set_safety_factor(gs_phys_body b, float factor) {
    PHYS_BODY(b, "set_safety_factor", );
    b3Body_SetSafetyFactor(id, factor);
}

float gs_phys_body_safety_factor(gs_phys_body b) {
    PHYS_BODY(b, "safety_factor", 0.0f);
    return b3Body_GetSafetyFactor(id);
}

uint8_t gs_phys_body_is_enabled(gs_phys_body b) {
    PHYS_BODY(b, "is_enabled", 0);
    return b3Body_IsEnabled(id);
}

void gs_phys_body_disable(gs_phys_body b) {
    PHYS_BODY(b, "disable", );
    b3Body_Disable(id);
}

void gs_phys_body_enable(gs_phys_body b) {
    PHYS_BODY(b, "enable", );
    b3Body_Enable(id);
}

void gs_phys_body_set_motion_locks(gs_phys_body b, gs_phys_motion_locks locks) {
    PHYS_BODY(b, "set_motion_locks", );
    b3MotionLocks m = { locks.linear_x, locks.linear_y, locks.linear_z,
                        locks.angular_x, locks.angular_y, locks.angular_z };
    b3Body_SetMotionLocks(id, m);
}

gs_phys_motion_locks gs_phys_body_motion_locks(gs_phys_body b) {
    gs_phys_motion_locks r = { 0, 0, 0, 0, 0, 0 };
    PHYS_BODY(b, "motion_locks", r);
    b3MotionLocks m = b3Body_GetMotionLocks(id);
    r.linear_x = m.linearX;
    r.linear_y = m.linearY;
    r.linear_z = m.linearZ;
    r.angular_x = m.angularX;
    r.angular_y = m.angularY;
    r.angular_z = m.angularZ;
    return r;
}

void gs_phys_body_set_bullet(gs_phys_body b, uint8_t flag) {
    PHYS_BODY(b, "set_bullet", );
    b3Body_SetBullet(id, flag);
}

uint8_t gs_phys_body_is_bullet(gs_phys_body b) {
    PHYS_BODY(b, "is_bullet", 0);
    return b3Body_IsBullet(id);
}

void gs_phys_body_allow_fast_rotation(gs_phys_body b, uint8_t flag) {
    PHYS_BODY(b, "allow_fast_rotation", );
    b3Body_AllowFastRotation(id, flag);
}

uint8_t gs_phys_body_fast_rotation_allowed(gs_phys_body b) {
    PHYS_BODY(b, "fast_rotation_allowed", 0);
    return b3Body_IsFastRotationAllowed(id);
}

void gs_phys_body_enable_contact_recycling(gs_phys_body b, uint8_t flag) {
    PHYS_BODY(b, "enable_contact_recycling", );
    b3Body_EnableContactRecycling(id, flag);
}

uint8_t gs_phys_body_contact_recycling_enabled(gs_phys_body b) {
    PHYS_BODY(b, "contact_recycling_enabled", 0);
    return b3Body_IsContactRecyclingEnabled(id);
}

void gs_phys_body_enable_hit_events(gs_phys_body b, uint8_t flag) {
    PHYS_BODY(b, "enable_hit_events", );
    b3Body_EnableHitEvents(id, flag);
}

gs_phys_world gs_phys_body_world(gs_phys_body b) {
    gs_phys_world none = { 0 };
    PHYS_BODY(b, "world", none);
    return phys_world_of(b3Body_GetWorld(id));
}

int64_t gs_phys_body_shape_count(gs_phys_body b) {
    PHYS_BODY(b, "shape_count", 0);
    return b3Body_GetShapeCount(id);
}

int64_t gs_phys_body_shapes(gs_phys_body b, gs_phys_shape_slice out) {
    PHYS_BODY(b, "shapes", 0);
    int n = b3Body_GetShapeCount(id);
    if (n == 0) return 0;
    b3ShapeId *shapes = (b3ShapeId *)malloc((size_t)n * sizeof(b3ShapeId));
    n = b3Body_GetShapes(id, shapes, n);
    for (int64_t i = 0; i < n && i < out.len; i++) out.data[i] = phys_shape_of(shapes[i]);
    free(shapes);
    return n;
}

int64_t gs_phys_body_joint_count(gs_phys_body b) {
    PHYS_BODY(b, "joint_count", 0);
    return b3Body_GetJointCount(id);
}

int64_t gs_phys_body_joints(gs_phys_body b, gs_phys_joint_slice out) {
    PHYS_BODY(b, "joints", 0);
    int n = b3Body_GetJointCount(id);
    if (n == 0) return 0;
    b3JointId *joints = (b3JointId *)malloc((size_t)n * sizeof(b3JointId));
    n = b3Body_GetJoints(id, joints, n);
    for (int64_t i = 0; i < n && i < out.len; i++) out.data[i] = phys_joint_of(joints[i]);
    free(joints);
    return n;
}

int64_t gs_phys_body_contacts(gs_phys_body b, gs_phys_manifold_slice out) {
    PHYS_BODY(b, "contacts", 0);
    int cap = b3Body_GetContactCapacity(id);
    if (cap == 0) return 0;
    b3ContactData *data = (b3ContactData *)malloc((size_t)cap * sizeof(b3ContactData));
    int count = b3Body_GetContactData(id, data, cap);
    int64_t n = 0;
    for (int i = 0; i < count; i++) phys_put_manifolds(data[i], out, &n);
    free(data);
    return n;
}

gs_phys_aabb gs_phys_body_aabb(gs_phys_body b) {
    gs_phys_aabb none;
    memset(&none, 0, sizeof none);
    PHYS_BODY(b, "aabb", none);
    return gsbox(b3Body_ComputeAABB(id));
}

float gs_phys_body_min_extent(gs_phys_body b) {
    PHYS_BODY(b, "min_extent", 0.0f);
    return b3Body_GetMinExtent(id);
}

gs_phys_float3 gs_phys_body_max_extent(gs_phys_body b) {
    PHYS_BODY(b, "max_extent", phys_zero3());
    return gsf3(b3Body_GetMaxExtent(id));
}

gs_phys_float3 gs_phys_body_max_extent_origin(gs_phys_body b) {
    PHYS_BODY(b, "max_extent_origin", phys_zero3());
    return gsf3(b3Body_GetMaxExtentOrigin(id));
}

float gs_phys_body_closest_point(gs_phys_body b, gs_phys_float3 target, gs_phys_float3 *result) {
    *result = phys_zero3();
    PHYS_BODY(b, "closest_point", 0.0f);
    b3Vec3 p;
    float d = b3Body_GetClosestPoint(id, &p, b3v3(target));
    *result = gsf3(p);
    return d;
}

static gs_phys_ray_hit phys_body_hit(b3BodyCastResult r) {
    gs_phys_ray_hit h;
    memset(&h, 0, sizeof h);
    if (!r.hit) return h;
    h.shape = phys_shape_of(r.shapeId);
    h.point = gsf3(r.point);
    h.normal = gsf3(r.normal);
    h.fraction = r.fraction;
    h.material_id = r.userMaterialId;
    h.triangle_index = r.triangleIndex;
    h.child_index = -1;
    h.hit = 1;
    return h;
}

gs_phys_ray_hit gs_phys_body_cast_ray(gs_phys_body b, gs_phys_float3 origin,
                                      gs_phys_float3 translation, gs_phys_query_filter filter,
                                      float max_fraction, gs_phys_transform body_transform) {
    gs_phys_ray_hit none;
    memset(&none, 0, sizeof none);
    PHYS_BODY(b, "cast_ray", none);
    return phys_body_hit(b3Body_CastRay(id, b3v3(origin), b3v3(translation), b3qfilter(filter),
                                        max_fraction, b3xf(body_transform)));
}

gs_phys_ray_hit gs_phys_body_cast_shape(gs_phys_body b, gs_phys_float3 origin,
                                        gs_phys_float3_slice points, float radius,
                                        gs_phys_float3 translation, gs_phys_query_filter filter,
                                        float max_fraction, uint8_t can_encroach,
                                        gs_phys_transform body_transform) {
    gs_phys_ray_hit none;
    memset(&none, 0, sizeof none);
    PHYS_BODY(b, "cast_shape", none);
    phys_proxy p;
    if (!phys_make_proxy(&p, points, radius, "cast_shape")) return none;
    return phys_body_hit(b3Body_CastShape(id, b3v3(origin), &p.proxy, b3v3(translation),
                                          b3qfilter(filter), max_fraction, can_encroach,
                                          b3xf(body_transform)));
}

uint8_t gs_phys_body_overlap_shape(gs_phys_body b, gs_phys_float3 origin,
                                   gs_phys_float3_slice points, float radius,
                                   gs_phys_query_filter filter, gs_phys_transform body_transform) {
    PHYS_BODY(b, "overlap_shape", 0);
    phys_proxy p;
    if (!phys_make_proxy(&p, points, radius, "overlap_shape")) return 0;
    return b3Body_OverlapShape(id, b3v3(origin), &p.proxy, b3qfilter(filter), b3xf(body_transform));
}

int64_t gs_phys_body_collide_mover(gs_phys_body b, gs_phys_float3 origin, gs_phys_capsule mover,
                                   gs_phys_query_filter filter, gs_phys_transform body_transform,
                                   gs_phys_plane_hit_slice out) {
    PHYS_BODY(b, "collide_mover", 0);
    /* Box3D finds at most one plane per shape. */
    int cap = b3Body_GetShapeCount(id);
    if (cap == 0) return 0;
    b3BodyPlaneResult *planes =
        (b3BodyPlaneResult *)malloc((size_t)cap * sizeof(b3BodyPlaneResult));
    b3Capsule c = b3capsule(mover);
    int n = b3Body_CollideMover(id, planes, cap, b3v3(origin), &c, b3qfilter(filter),
                                b3xf(body_transform));
    for (int64_t i = 0; i < n && i < out.len; i++) {
        gs_phys_plane_hit *o = &out.data[i];
        o->shape = phys_shape_of(planes[i].shapeId);
        o->plane = gsplane(planes[i].result.plane);
        o->point = gsf3(planes[i].result.point);
        o->triangle_index = planes[i].result.triangleIndex;
        o->child_index = planes[i].result.childIndex;
        o->material_index = planes[i].result.materialIndex;
    }
    free(planes);
    return n;
}

gs_phys_toi_hit gs_phys_body_time_of_impact_mover(gs_phys_body b, gs_phys_float3 origin,
                                                  gs_phys_capsule mover, gs_phys_float3 translation,
                                                  gs_phys_query_filter filter,
                                                  gs_phys_transform transform1,
                                                  gs_phys_transform transform2) {
    gs_phys_toi_hit none;
    memset(&none, 0, sizeof none);
    none.fraction = 1.0f;
    PHYS_BODY(b, "time_of_impact_mover", none);
    b3Capsule c = b3capsule(mover);
    b3BodyTOIResult r = b3Body_TimeOfImpactMover(id, b3v3(origin), &c, b3v3(translation),
                                                 b3qfilter(filter), b3xf(transform1),
                                                 b3xf(transform2));
    gs_phys_toi_hit h = { gsf3(r.point), gsf3(r.normal), r.fraction, phys_shape_of(r.shapeId) };
    return h;
}
