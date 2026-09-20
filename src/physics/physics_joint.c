/* Joints: creating each kind, what every joint has, and each kind's own
   settings, which check the joint is of that kind before Box3D sees it. */

#include "physics_internal.h"

/* --- creation ------------------------------------------------------------- */

/* The checks Box3D asserts on and would otherwise index past: both bodies
   exist, in this world, and are two. */
static bool phys_joint_start(gs_phys_world w, const gs_phys_joint_def *def, const char *fn,
                             b3WorldId *world) {
    b3BodyId a, b;
    if (!phys_world_id(w, fn, world) || !phys_body_id(def->body_a, fn, &a) ||
        !phys_body_id(def->body_b, fn, &b))
        return false;
    if (a.world0 != world->index1 - 1 || b.world0 != world->index1 - 1)
        return phys_misuse("physics::%s: the bodies are in another world", fn);
    if (def->body_a.id == def->body_b.id)
        return phys_misuse("physics::%s: a joint between a body and itself", fn);
    return true;
}

#define PHYS_JOINT_START(w, def, fn, handle) \
    handle none; \
    memset(&none, 0, sizeof none); \
    b3WorldId world; \
    if (!phys_joint_start(w, &(def).base, fn, &world)) return none

gs_phys_distance_joint gs_phys_create_distance_joint(gs_phys_world w,
                                                     gs_phys_distance_joint_def def) {
    PHYS_JOINT_START(w, def, "create_distance_joint", gs_phys_distance_joint);
    b3DistanceJointDef d = b3DefaultDistanceJointDef();
    d.base = phys_joint_def(&def.base, d.base);
    d.length = def.length;
    d.enableSpring = def.enable_spring;
    d.lowerSpringForce = def.lower_spring_force;
    d.upperSpringForce = def.upper_spring_force;
    d.hertz = def.hertz;
    d.dampingRatio = def.damping_ratio;
    d.enableLimit = def.enable_limit;
    d.minLength = def.min_length;
    d.maxLength = def.max_length;
    d.enableMotor = def.enable_motor;
    d.maxMotorForce = def.max_motor_force;
    d.motorSpeed = def.motor_speed;
    gs_phys_distance_joint j = { phys_joint_of(b3CreateDistanceJoint(world, &d)) };
    return j;
}

gs_phys_filter_joint gs_phys_create_filter_joint(gs_phys_world w, gs_phys_filter_joint_def def) {
    PHYS_JOINT_START(w, def, "create_filter_joint", gs_phys_filter_joint);
    b3FilterJointDef d = b3DefaultFilterJointDef();
    d.base = phys_joint_def(&def.base, d.base);
    gs_phys_filter_joint j = { phys_joint_of(b3CreateFilterJoint(world, &d)) };
    return j;
}

gs_phys_motor_joint gs_phys_create_motor_joint(gs_phys_world w, gs_phys_motor_joint_def def) {
    PHYS_JOINT_START(w, def, "create_motor_joint", gs_phys_motor_joint);
    b3MotorJointDef d = b3DefaultMotorJointDef();
    d.base = phys_joint_def(&def.base, d.base);
    d.linearVelocity = b3v3(def.linear_velocity);
    d.maxVelocityForce = def.max_velocity_force;
    d.angularVelocity = b3v3(def.angular_velocity);
    d.maxVelocityTorque = def.max_velocity_torque;
    d.linearHertz = def.linear_hertz;
    d.linearDampingRatio = def.linear_damping_ratio;
    d.maxSpringForce = def.max_spring_force;
    d.angularHertz = def.angular_hertz;
    d.angularDampingRatio = def.angular_damping_ratio;
    d.maxSpringTorque = def.max_spring_torque;
    gs_phys_motor_joint j = { phys_joint_of(b3CreateMotorJoint(world, &d)) };
    return j;
}

gs_phys_parallel_joint gs_phys_create_parallel_joint(gs_phys_world w,
                                                     gs_phys_parallel_joint_def def) {
    PHYS_JOINT_START(w, def, "create_parallel_joint", gs_phys_parallel_joint);
    b3ParallelJointDef d = b3DefaultParallelJointDef();
    d.base = phys_joint_def(&def.base, d.base);
    d.hertz = def.hertz;
    d.dampingRatio = def.damping_ratio;
    d.maxTorque = def.max_torque;
    gs_phys_parallel_joint j = { phys_joint_of(b3CreateParallelJoint(world, &d)) };
    return j;
}

gs_phys_prismatic_joint gs_phys_create_prismatic_joint(gs_phys_world w,
                                                       gs_phys_prismatic_joint_def def) {
    PHYS_JOINT_START(w, def, "create_prismatic_joint", gs_phys_prismatic_joint);
    b3PrismaticJointDef d = b3DefaultPrismaticJointDef();
    d.base = phys_joint_def(&def.base, d.base);
    d.enableSpring = def.enable_spring;
    d.hertz = def.hertz;
    d.dampingRatio = def.damping_ratio;
    d.targetTranslation = def.target_translation;
    d.enableLimit = def.enable_limit;
    d.lowerTranslation = def.lower_translation;
    d.upperTranslation = def.upper_translation;
    d.enableMotor = def.enable_motor;
    d.maxMotorForce = def.max_motor_force;
    d.motorSpeed = def.motor_speed;
    gs_phys_prismatic_joint j = { phys_joint_of(b3CreatePrismaticJoint(world, &d)) };
    return j;
}

gs_phys_revolute_joint gs_phys_create_revolute_joint(gs_phys_world w,
                                                     gs_phys_revolute_joint_def def) {
    PHYS_JOINT_START(w, def, "create_revolute_joint", gs_phys_revolute_joint);
    b3RevoluteJointDef d = b3DefaultRevoluteJointDef();
    d.base = phys_joint_def(&def.base, d.base);
    d.targetAngle = def.target_angle;
    d.enableSpring = def.enable_spring;
    d.hertz = def.hertz;
    d.dampingRatio = def.damping_ratio;
    d.enableLimit = def.enable_limit;
    d.lowerAngle = def.lower_angle;
    d.upperAngle = def.upper_angle;
    d.enableMotor = def.enable_motor;
    d.maxMotorTorque = def.max_motor_torque;
    d.motorSpeed = def.motor_speed;
    gs_phys_revolute_joint j = { phys_joint_of(b3CreateRevoluteJoint(world, &d)) };
    return j;
}

gs_phys_spherical_joint gs_phys_create_spherical_joint(gs_phys_world w,
                                                       gs_phys_spherical_joint_def def) {
    PHYS_JOINT_START(w, def, "create_spherical_joint", gs_phys_spherical_joint);
    b3SphericalJointDef d = b3DefaultSphericalJointDef();
    d.base = phys_joint_def(&def.base, d.base);
    d.enableSpring = def.enable_spring;
    d.hertz = def.hertz;
    d.dampingRatio = def.damping_ratio;
    d.targetRotation = b3q(def.target_rotation);
    d.enableConeLimit = def.enable_cone_limit;
    d.coneAngle = def.cone_angle;
    d.enableTwistLimit = def.enable_twist_limit;
    d.lowerTwistAngle = def.lower_twist_angle;
    d.upperTwistAngle = def.upper_twist_angle;
    d.enableMotor = def.enable_motor;
    d.maxMotorTorque = def.max_motor_torque;
    d.motorVelocity = b3v3(def.motor_velocity);
    gs_phys_spherical_joint j = { phys_joint_of(b3CreateSphericalJoint(world, &d)) };
    return j;
}

gs_phys_weld_joint gs_phys_create_weld_joint(gs_phys_world w, gs_phys_weld_joint_def def) {
    PHYS_JOINT_START(w, def, "create_weld_joint", gs_phys_weld_joint);
    b3WeldJointDef d = b3DefaultWeldJointDef();
    d.base = phys_joint_def(&def.base, d.base);
    d.linearHertz = def.linear_hertz;
    d.angularHertz = def.angular_hertz;
    d.linearDampingRatio = def.linear_damping_ratio;
    d.angularDampingRatio = def.angular_damping_ratio;
    gs_phys_weld_joint j = { phys_joint_of(b3CreateWeldJoint(world, &d)) };
    return j;
}

gs_phys_wheel_joint gs_phys_create_wheel_joint(gs_phys_world w, gs_phys_wheel_joint_def def) {
    PHYS_JOINT_START(w, def, "create_wheel_joint", gs_phys_wheel_joint);
    b3WheelJointDef d = b3DefaultWheelJointDef();
    d.base = phys_joint_def(&def.base, d.base);
    d.enableSuspensionSpring = def.enable_suspension_spring;
    d.suspensionHertz = def.suspension_hertz;
    d.suspensionDampingRatio = def.suspension_damping_ratio;
    d.enableSuspensionLimit = def.enable_suspension_limit;
    d.lowerSuspensionLimit = def.lower_suspension_limit;
    d.upperSuspensionLimit = def.upper_suspension_limit;
    d.enableSpinMotor = def.enable_spin_motor;
    d.maxSpinTorque = def.max_spin_torque;
    d.spinSpeed = def.spin_speed;
    d.enableSteering = def.enable_steering;
    d.steeringHertz = def.steering_hertz;
    d.steeringDampingRatio = def.steering_damping_ratio;
    d.targetSteeringAngle = def.target_steering_angle;
    d.maxSteeringTorque = def.max_steering_torque;
    d.enableSteeringLimit = def.enable_steering_limit;
    d.lowerSteeringLimit = def.lower_steering_limit;
    d.upperSteeringLimit = def.upper_steering_limit;
    gs_phys_wheel_joint j = { phys_joint_of(b3CreateWheelJoint(world, &d)) };
    return j;
}

/* --- Box3D's defaults ----------------------------------------------------- */

gs_phys_distance_joint_def gs_phys_default_distance_joint_def(void) {
    b3DistanceJointDef d = b3DefaultDistanceJointDef();
    gs_phys_distance_joint_def r;
    memset(&r, 0, sizeof r);
    r.base = phys_joint_def_of(d.base);
    r.length = d.length;
    r.enable_spring = d.enableSpring;
    r.lower_spring_force = d.lowerSpringForce;
    r.upper_spring_force = d.upperSpringForce;
    r.hertz = d.hertz;
    r.damping_ratio = d.dampingRatio;
    r.enable_limit = d.enableLimit;
    r.min_length = d.minLength;
    r.max_length = d.maxLength;
    r.enable_motor = d.enableMotor;
    r.max_motor_force = d.maxMotorForce;
    r.motor_speed = d.motorSpeed;
    return r;
}

gs_phys_filter_joint_def gs_phys_default_filter_joint_def(void) {
    gs_phys_filter_joint_def r = { phys_joint_def_of(b3DefaultFilterJointDef().base) };
    return r;
}

gs_phys_motor_joint_def gs_phys_default_motor_joint_def(void) {
    b3MotorJointDef d = b3DefaultMotorJointDef();
    gs_phys_motor_joint_def r;
    memset(&r, 0, sizeof r);
    r.base = phys_joint_def_of(d.base);
    r.linear_velocity = gsf3(d.linearVelocity);
    r.max_velocity_force = d.maxVelocityForce;
    r.angular_velocity = gsf3(d.angularVelocity);
    r.max_velocity_torque = d.maxVelocityTorque;
    r.linear_hertz = d.linearHertz;
    r.linear_damping_ratio = d.linearDampingRatio;
    r.max_spring_force = d.maxSpringForce;
    r.angular_hertz = d.angularHertz;
    r.angular_damping_ratio = d.angularDampingRatio;
    r.max_spring_torque = d.maxSpringTorque;
    return r;
}

gs_phys_parallel_joint_def gs_phys_default_parallel_joint_def(void) {
    b3ParallelJointDef d = b3DefaultParallelJointDef();
    gs_phys_parallel_joint_def r;
    memset(&r, 0, sizeof r);
    r.base = phys_joint_def_of(d.base);
    r.hertz = d.hertz;
    r.damping_ratio = d.dampingRatio;
    r.max_torque = d.maxTorque;
    return r;
}

gs_phys_prismatic_joint_def gs_phys_default_prismatic_joint_def(void) {
    b3PrismaticJointDef d = b3DefaultPrismaticJointDef();
    gs_phys_prismatic_joint_def r;
    memset(&r, 0, sizeof r);
    r.base = phys_joint_def_of(d.base);
    r.enable_spring = d.enableSpring;
    r.hertz = d.hertz;
    r.damping_ratio = d.dampingRatio;
    r.target_translation = d.targetTranslation;
    r.enable_limit = d.enableLimit;
    r.lower_translation = d.lowerTranslation;
    r.upper_translation = d.upperTranslation;
    r.enable_motor = d.enableMotor;
    r.max_motor_force = d.maxMotorForce;
    r.motor_speed = d.motorSpeed;
    return r;
}

gs_phys_revolute_joint_def gs_phys_default_revolute_joint_def(void) {
    b3RevoluteJointDef d = b3DefaultRevoluteJointDef();
    gs_phys_revolute_joint_def r;
    memset(&r, 0, sizeof r);
    r.base = phys_joint_def_of(d.base);
    r.target_angle = d.targetAngle;
    r.enable_spring = d.enableSpring;
    r.hertz = d.hertz;
    r.damping_ratio = d.dampingRatio;
    r.enable_limit = d.enableLimit;
    r.lower_angle = d.lowerAngle;
    r.upper_angle = d.upperAngle;
    r.enable_motor = d.enableMotor;
    r.max_motor_torque = d.maxMotorTorque;
    r.motor_speed = d.motorSpeed;
    return r;
}

gs_phys_spherical_joint_def gs_phys_default_spherical_joint_def(void) {
    b3SphericalJointDef d = b3DefaultSphericalJointDef();
    gs_phys_spherical_joint_def r;
    memset(&r, 0, sizeof r);
    r.base = phys_joint_def_of(d.base);
    r.enable_spring = d.enableSpring;
    r.hertz = d.hertz;
    r.damping_ratio = d.dampingRatio;
    r.target_rotation = gsq(d.targetRotation);
    r.enable_cone_limit = d.enableConeLimit;
    r.cone_angle = d.coneAngle;
    r.enable_twist_limit = d.enableTwistLimit;
    r.lower_twist_angle = d.lowerTwistAngle;
    r.upper_twist_angle = d.upperTwistAngle;
    r.enable_motor = d.enableMotor;
    r.max_motor_torque = d.maxMotorTorque;
    r.motor_velocity = gsf3(d.motorVelocity);
    return r;
}

gs_phys_weld_joint_def gs_phys_default_weld_joint_def(void) {
    b3WeldJointDef d = b3DefaultWeldJointDef();
    gs_phys_weld_joint_def r;
    memset(&r, 0, sizeof r);
    r.base = phys_joint_def_of(d.base);
    r.linear_hertz = d.linearHertz;
    r.angular_hertz = d.angularHertz;
    r.linear_damping_ratio = d.linearDampingRatio;
    r.angular_damping_ratio = d.angularDampingRatio;
    return r;
}

gs_phys_wheel_joint_def gs_phys_default_wheel_joint_def(void) {
    b3WheelJointDef d = b3DefaultWheelJointDef();
    gs_phys_wheel_joint_def r;
    memset(&r, 0, sizeof r);
    r.base = phys_joint_def_of(d.base);
    r.enable_suspension_spring = d.enableSuspensionSpring;
    r.suspension_hertz = d.suspensionHertz;
    r.suspension_damping_ratio = d.suspensionDampingRatio;
    r.enable_suspension_limit = d.enableSuspensionLimit;
    r.lower_suspension_limit = d.lowerSuspensionLimit;
    r.upper_suspension_limit = d.upperSuspensionLimit;
    r.enable_spin_motor = d.enableSpinMotor;
    r.max_spin_torque = d.maxSpinTorque;
    r.spin_speed = d.spinSpeed;
    r.enable_steering = d.enableSteering;
    r.steering_hertz = d.steeringHertz;
    r.steering_damping_ratio = d.steeringDampingRatio;
    r.target_steering_angle = d.targetSteeringAngle;
    r.max_steering_torque = d.maxSteeringTorque;
    r.enable_steering_limit = d.enableSteeringLimit;
    r.lower_steering_limit = d.lowerSteeringLimit;
    r.upper_steering_limit = d.upperSteeringLimit;
    return r;
}

/* --- every joint ---------------------------------------------------------- */

void gs_phys_joint_destroy(gs_phys_joint j, uint8_t wake_bodies) {
    PHYS_JOINT(j, "destroy", );
    b3DestroyJoint(id, wake_bodies);
}

uint8_t gs_phys_joint_is_valid(gs_phys_joint j) {
    return j.id && b3Joint_IsValid(b3LoadJointId(j.id));
}

int32_t gs_phys_joint_type(gs_phys_joint j) {
    PHYS_JOINT(j, "joint_type", -1);
    return b3Joint_GetType(id);
}

gs_phys_body gs_phys_joint_body_a(gs_phys_joint j) {
    gs_phys_body none = { 0 };
    PHYS_JOINT(j, "body_a", none);
    return phys_body_of(b3Joint_GetBodyA(id));
}

gs_phys_body gs_phys_joint_body_b(gs_phys_joint j) {
    gs_phys_body none = { 0 };
    PHYS_JOINT(j, "body_b", none);
    return phys_body_of(b3Joint_GetBodyB(id));
}

gs_phys_world gs_phys_joint_world(gs_phys_joint j) {
    gs_phys_world none = { 0 };
    PHYS_JOINT(j, "world", none);
    return phys_world_of(b3Joint_GetWorld(id));
}

void gs_phys_joint_set_local_frame_a(gs_phys_joint j, gs_phys_transform frame) {
    PHYS_JOINT(j, "set_local_frame_a", );
    b3Joint_SetLocalFrameA(id, b3xf(frame));
}

gs_phys_transform gs_phys_joint_local_frame_a(gs_phys_joint j) {
    gs_phys_transform none = { { 0, 0, 0 }, { 0, 0, 0, 1 } };
    PHYS_JOINT(j, "local_frame_a", none);
    return gsxf(b3Joint_GetLocalFrameA(id));
}

void gs_phys_joint_set_local_frame_b(gs_phys_joint j, gs_phys_transform frame) {
    PHYS_JOINT(j, "set_local_frame_b", );
    b3Joint_SetLocalFrameB(id, b3xf(frame));
}

gs_phys_transform gs_phys_joint_local_frame_b(gs_phys_joint j) {
    gs_phys_transform none = { { 0, 0, 0 }, { 0, 0, 0, 1 } };
    PHYS_JOINT(j, "local_frame_b", none);
    return gsxf(b3Joint_GetLocalFrameB(id));
}

void gs_phys_joint_set_collide_connected(gs_phys_joint j, uint8_t flag) {
    PHYS_JOINT(j, "set_collide_connected", );
    b3Joint_SetCollideConnected(id, flag);
}

uint8_t gs_phys_joint_collide_connected(gs_phys_joint j) {
    PHYS_JOINT(j, "collide_connected", 0);
    return b3Joint_GetCollideConnected(id);
}

void gs_phys_joint_set_user_data(gs_phys_joint j, uint64_t data) {
    PHYS_JOINT(j, "set_user_data", );
    b3Joint_SetUserData(id, (void *)(uintptr_t)data);
}

uint64_t gs_phys_joint_user_data(gs_phys_joint j) {
    PHYS_JOINT(j, "user_data", 0);
    return (uint64_t)(uintptr_t)b3Joint_GetUserData(id);
}

void gs_phys_joint_wake_bodies(gs_phys_joint j) {
    PHYS_JOINT(j, "wake_bodies", );
    b3Joint_WakeBodies(id);
}

uint8_t gs_phys_joint_is_awake(gs_phys_joint j) {
    PHYS_JOINT(j, "is_awake", 0);
    return b3Joint_IsAwake(id);
}

gs_phys_float3 gs_phys_joint_constraint_force(gs_phys_joint j) {
    gs_phys_float3 none = { 0, 0, 0 };
    PHYS_JOINT(j, "constraint_force", none);
    return gsf3(b3Joint_GetConstraintForce(id));
}

gs_phys_float3 gs_phys_joint_constraint_torque(gs_phys_joint j) {
    gs_phys_float3 none = { 0, 0, 0 };
    PHYS_JOINT(j, "constraint_torque", none);
    return gsf3(b3Joint_GetConstraintTorque(id));
}

float gs_phys_joint_linear_separation(gs_phys_joint j) {
    PHYS_JOINT(j, "linear_separation", 0.0f);
    return b3Joint_GetLinearSeparation(id);
}

float gs_phys_joint_angular_separation(gs_phys_joint j) {
    PHYS_JOINT(j, "angular_separation", 0.0f);
    return b3Joint_GetAngularSeparation(id);
}

void gs_phys_joint_set_constraint_tuning(gs_phys_joint j, float hertz, float damping_ratio) {
    PHYS_JOINT(j, "set_constraint_tuning", );
    b3Joint_SetConstraintTuning(id, hertz, damping_ratio);
}

gs_phys_float2 gs_phys_joint_constraint_tuning(gs_phys_joint j) {
    gs_phys_float2 r = { 0, 0 };
    PHYS_JOINT(j, "constraint_tuning", r);
    b3Joint_GetConstraintTuning(id, &r.x, &r.y);
    return r;
}

void gs_phys_joint_set_force_threshold(gs_phys_joint j, float threshold) {
    PHYS_JOINT(j, "set_force_threshold", );
    b3Joint_SetForceThreshold(id, threshold);
}

float gs_phys_joint_force_threshold(gs_phys_joint j) {
    PHYS_JOINT(j, "force_threshold", 0.0f);
    return b3Joint_GetForceThreshold(id);
}

void gs_phys_joint_set_torque_threshold(gs_phys_joint j, float threshold) {
    PHYS_JOINT(j, "set_torque_threshold", );
    b3Joint_SetTorqueThreshold(id, threshold);
}

float gs_phys_joint_torque_threshold(gs_phys_joint j) {
    PHYS_JOINT(j, "torque_threshold", 0.0f);
    return b3Joint_GetTorqueThreshold(id);
}

/* --- each kind's own ------------------------------------------------------
   All alike: check the joint and its kind, then call Box3D. */

#define GET_F(name, handle, kind, fn, call) \
    float name(handle j) { \
        PHYS_KIND(j, kind, fn, 0.0f); \
        return call(id); \
    }
#define SET_F(name, handle, kind, fn, call) \
    void name(handle j, float value) { \
        PHYS_KIND(j, kind, fn, ); \
        call(id, value); \
    }
#define GET_B(name, handle, kind, fn, call) \
    uint8_t name(handle j) { \
        PHYS_KIND(j, kind, fn, 0); \
        return call(id); \
    }
#define SET_B(name, handle, kind, fn, call) \
    void name(handle j, uint8_t flag) { \
        PHYS_KIND(j, kind, fn, ); \
        call(id, flag); \
    }
#define GET_V(name, handle, kind, fn, call) \
    gs_phys_float3 name(handle j) { \
        gs_phys_float3 none = { 0, 0, 0 }; \
        PHYS_KIND(j, kind, fn, none); \
        return gsf3(call(id)); \
    }
#define SET_V(name, handle, kind, fn, call) \
    void name(handle j, gs_phys_float3 value) { \
        PHYS_KIND(j, kind, fn, ); \
        call(id, b3v3(value)); \
    }
#define SET_RANGE(name, handle, kind, fn, call) \
    void name(handle j, float lower, float upper) { \
        PHYS_KIND(j, kind, fn, ); \
        call(id, lower, upper); \
    }

SET_F(gs_phys_distance_set_rest_length, gs_phys_distance_joint, b3_distanceJoint, "set_rest_length",
      b3DistanceJoint_SetLength)
GET_F(gs_phys_distance_rest_length, gs_phys_distance_joint, b3_distanceJoint, "rest_length",
      b3DistanceJoint_GetLength)
SET_B(gs_phys_distance_enable_spring, gs_phys_distance_joint, b3_distanceJoint, "enable_spring",
      b3DistanceJoint_EnableSpring)
GET_B(gs_phys_distance_spring_enabled, gs_phys_distance_joint, b3_distanceJoint, "spring_enabled",
      b3DistanceJoint_IsSpringEnabled)
SET_RANGE(gs_phys_distance_set_spring_force_range, gs_phys_distance_joint, b3_distanceJoint,
          "set_spring_force_range", b3DistanceJoint_SetSpringForceRange)
SET_F(gs_phys_distance_set_spring_hertz, gs_phys_distance_joint, b3_distanceJoint,
      "set_spring_hertz", b3DistanceJoint_SetSpringHertz)
GET_F(gs_phys_distance_spring_hertz, gs_phys_distance_joint, b3_distanceJoint, "spring_hertz",
      b3DistanceJoint_GetSpringHertz)
SET_F(gs_phys_distance_set_spring_damping_ratio, gs_phys_distance_joint, b3_distanceJoint,
      "set_spring_damping_ratio", b3DistanceJoint_SetSpringDampingRatio)
GET_F(gs_phys_distance_spring_damping_ratio, gs_phys_distance_joint, b3_distanceJoint,
      "spring_damping_ratio", b3DistanceJoint_GetSpringDampingRatio)
SET_B(gs_phys_distance_enable_limit, gs_phys_distance_joint, b3_distanceJoint, "enable_limit",
      b3DistanceJoint_EnableLimit)
GET_B(gs_phys_distance_limit_enabled, gs_phys_distance_joint, b3_distanceJoint, "limit_enabled",
      b3DistanceJoint_IsLimitEnabled)
SET_RANGE(gs_phys_distance_set_length_range, gs_phys_distance_joint, b3_distanceJoint,
          "set_length_range", b3DistanceJoint_SetLengthRange)
GET_F(gs_phys_distance_min_length, gs_phys_distance_joint, b3_distanceJoint, "min_length",
      b3DistanceJoint_GetMinLength)
GET_F(gs_phys_distance_max_length, gs_phys_distance_joint, b3_distanceJoint, "max_length",
      b3DistanceJoint_GetMaxLength)
GET_F(gs_phys_distance_current_length, gs_phys_distance_joint, b3_distanceJoint, "current_length",
      b3DistanceJoint_GetCurrentLength)
SET_B(gs_phys_distance_enable_motor, gs_phys_distance_joint, b3_distanceJoint, "enable_motor",
      b3DistanceJoint_EnableMotor)
GET_B(gs_phys_distance_motor_enabled, gs_phys_distance_joint, b3_distanceJoint, "motor_enabled",
      b3DistanceJoint_IsMotorEnabled)
SET_F(gs_phys_distance_set_motor_speed, gs_phys_distance_joint, b3_distanceJoint, "set_motor_speed",
      b3DistanceJoint_SetMotorSpeed)
GET_F(gs_phys_distance_motor_speed, gs_phys_distance_joint, b3_distanceJoint, "motor_speed",
      b3DistanceJoint_GetMotorSpeed)
SET_F(gs_phys_distance_set_max_motor_force, gs_phys_distance_joint, b3_distanceJoint,
      "set_max_motor_force", b3DistanceJoint_SetMaxMotorForce)
GET_F(gs_phys_distance_max_motor_force, gs_phys_distance_joint, b3_distanceJoint, "max_motor_force",
      b3DistanceJoint_GetMaxMotorForce)
GET_F(gs_phys_distance_motor_force, gs_phys_distance_joint, b3_distanceJoint, "motor_force",
      b3DistanceJoint_GetMotorForce)

gs_phys_float2 gs_phys_distance_spring_force_range(gs_phys_distance_joint j) {
    gs_phys_float2 r = { 0, 0 };
    PHYS_KIND(j, b3_distanceJoint, "spring_force_range", r);
    b3DistanceJoint_GetSpringForceRange(id, &r.x, &r.y);
    return r;
}

SET_V(gs_phys_motor_set_linear_velocity, gs_phys_motor_joint, b3_motorJoint, "set_linear_velocity",
      b3MotorJoint_SetLinearVelocity)
GET_V(gs_phys_motor_linear_velocity, gs_phys_motor_joint, b3_motorJoint, "linear_velocity",
      b3MotorJoint_GetLinearVelocity)
SET_V(gs_phys_motor_set_angular_velocity, gs_phys_motor_joint, b3_motorJoint,
      "set_angular_velocity", b3MotorJoint_SetAngularVelocity)
GET_V(gs_phys_motor_angular_velocity, gs_phys_motor_joint, b3_motorJoint, "angular_velocity",
      b3MotorJoint_GetAngularVelocity)
SET_F(gs_phys_motor_set_max_velocity_force, gs_phys_motor_joint, b3_motorJoint,
      "set_max_velocity_force", b3MotorJoint_SetMaxVelocityForce)
GET_F(gs_phys_motor_max_velocity_force, gs_phys_motor_joint, b3_motorJoint, "max_velocity_force",
      b3MotorJoint_GetMaxVelocityForce)
SET_F(gs_phys_motor_set_max_velocity_torque, gs_phys_motor_joint, b3_motorJoint,
      "set_max_velocity_torque", b3MotorJoint_SetMaxVelocityTorque)
GET_F(gs_phys_motor_max_velocity_torque, gs_phys_motor_joint, b3_motorJoint, "max_velocity_torque",
      b3MotorJoint_GetMaxVelocityTorque)
SET_F(gs_phys_motor_set_linear_hertz, gs_phys_motor_joint, b3_motorJoint, "set_linear_hertz",
      b3MotorJoint_SetLinearHertz)
GET_F(gs_phys_motor_linear_hertz, gs_phys_motor_joint, b3_motorJoint, "linear_hertz",
      b3MotorJoint_GetLinearHertz)
SET_F(gs_phys_motor_set_linear_damping_ratio, gs_phys_motor_joint, b3_motorJoint,
      "set_linear_damping_ratio", b3MotorJoint_SetLinearDampingRatio)
GET_F(gs_phys_motor_linear_damping_ratio, gs_phys_motor_joint, b3_motorJoint,
      "linear_damping_ratio", b3MotorJoint_GetLinearDampingRatio)
SET_F(gs_phys_motor_set_angular_hertz, gs_phys_motor_joint, b3_motorJoint, "set_angular_hertz",
      b3MotorJoint_SetAngularHertz)
GET_F(gs_phys_motor_angular_hertz, gs_phys_motor_joint, b3_motorJoint, "angular_hertz",
      b3MotorJoint_GetAngularHertz)
SET_F(gs_phys_motor_set_angular_damping_ratio, gs_phys_motor_joint, b3_motorJoint,
      "set_angular_damping_ratio", b3MotorJoint_SetAngularDampingRatio)
GET_F(gs_phys_motor_angular_damping_ratio, gs_phys_motor_joint, b3_motorJoint,
      "angular_damping_ratio", b3MotorJoint_GetAngularDampingRatio)
SET_F(gs_phys_motor_set_max_spring_force, gs_phys_motor_joint, b3_motorJoint,
      "set_max_spring_force", b3MotorJoint_SetMaxSpringForce)
GET_F(gs_phys_motor_max_spring_force, gs_phys_motor_joint, b3_motorJoint, "max_spring_force",
      b3MotorJoint_GetMaxSpringForce)
SET_F(gs_phys_motor_set_max_spring_torque, gs_phys_motor_joint, b3_motorJoint,
      "set_max_spring_torque", b3MotorJoint_SetMaxSpringTorque)
GET_F(gs_phys_motor_max_spring_torque, gs_phys_motor_joint, b3_motorJoint, "max_spring_torque",
      b3MotorJoint_GetMaxSpringTorque)

SET_F(gs_phys_parallel_set_spring_hertz, gs_phys_parallel_joint, b3_parallelJoint,
      "set_spring_hertz", b3ParallelJoint_SetSpringHertz)
GET_F(gs_phys_parallel_spring_hertz, gs_phys_parallel_joint, b3_parallelJoint, "spring_hertz",
      b3ParallelJoint_GetSpringHertz)
SET_F(gs_phys_parallel_set_spring_damping_ratio, gs_phys_parallel_joint, b3_parallelJoint,
      "set_spring_damping_ratio", b3ParallelJoint_SetSpringDampingRatio)
GET_F(gs_phys_parallel_spring_damping_ratio, gs_phys_parallel_joint, b3_parallelJoint,
      "spring_damping_ratio", b3ParallelJoint_GetSpringDampingRatio)
SET_F(gs_phys_parallel_set_max_torque, gs_phys_parallel_joint, b3_parallelJoint, "set_max_torque",
      b3ParallelJoint_SetMaxTorque)
GET_F(gs_phys_parallel_max_torque, gs_phys_parallel_joint, b3_parallelJoint, "max_torque",
      b3ParallelJoint_GetMaxTorque)

SET_B(gs_phys_prismatic_enable_spring, gs_phys_prismatic_joint, b3_prismaticJoint, "enable_spring",
      b3PrismaticJoint_EnableSpring)
GET_B(gs_phys_prismatic_spring_enabled, gs_phys_prismatic_joint, b3_prismaticJoint,
      "spring_enabled", b3PrismaticJoint_IsSpringEnabled)
SET_F(gs_phys_prismatic_set_spring_hertz, gs_phys_prismatic_joint, b3_prismaticJoint,
      "set_spring_hertz", b3PrismaticJoint_SetSpringHertz)
GET_F(gs_phys_prismatic_spring_hertz, gs_phys_prismatic_joint, b3_prismaticJoint, "spring_hertz",
      b3PrismaticJoint_GetSpringHertz)
SET_F(gs_phys_prismatic_set_spring_damping_ratio, gs_phys_prismatic_joint, b3_prismaticJoint,
      "set_spring_damping_ratio", b3PrismaticJoint_SetSpringDampingRatio)
GET_F(gs_phys_prismatic_spring_damping_ratio, gs_phys_prismatic_joint, b3_prismaticJoint,
      "spring_damping_ratio", b3PrismaticJoint_GetSpringDampingRatio)
SET_F(gs_phys_prismatic_set_target_translation, gs_phys_prismatic_joint, b3_prismaticJoint,
      "set_target_translation", b3PrismaticJoint_SetTargetTranslation)
GET_F(gs_phys_prismatic_target_translation, gs_phys_prismatic_joint, b3_prismaticJoint,
      "target_translation", b3PrismaticJoint_GetTargetTranslation)
SET_B(gs_phys_prismatic_enable_limit, gs_phys_prismatic_joint, b3_prismaticJoint, "enable_limit",
      b3PrismaticJoint_EnableLimit)
GET_B(gs_phys_prismatic_limit_enabled, gs_phys_prismatic_joint, b3_prismaticJoint, "limit_enabled",
      b3PrismaticJoint_IsLimitEnabled)
GET_F(gs_phys_prismatic_lower_limit, gs_phys_prismatic_joint, b3_prismaticJoint, "lower_limit",
      b3PrismaticJoint_GetLowerLimit)
GET_F(gs_phys_prismatic_upper_limit, gs_phys_prismatic_joint, b3_prismaticJoint, "upper_limit",
      b3PrismaticJoint_GetUpperLimit)
SET_RANGE(gs_phys_prismatic_set_limits, gs_phys_prismatic_joint, b3_prismaticJoint, "set_limits",
          b3PrismaticJoint_SetLimits)
SET_B(gs_phys_prismatic_enable_motor, gs_phys_prismatic_joint, b3_prismaticJoint, "enable_motor",
      b3PrismaticJoint_EnableMotor)
GET_B(gs_phys_prismatic_motor_enabled, gs_phys_prismatic_joint, b3_prismaticJoint, "motor_enabled",
      b3PrismaticJoint_IsMotorEnabled)
SET_F(gs_phys_prismatic_set_motor_speed, gs_phys_prismatic_joint, b3_prismaticJoint,
      "set_motor_speed", b3PrismaticJoint_SetMotorSpeed)
GET_F(gs_phys_prismatic_motor_speed, gs_phys_prismatic_joint, b3_prismaticJoint, "motor_speed",
      b3PrismaticJoint_GetMotorSpeed)
SET_F(gs_phys_prismatic_set_max_motor_force, gs_phys_prismatic_joint, b3_prismaticJoint,
      "set_max_motor_force", b3PrismaticJoint_SetMaxMotorForce)
GET_F(gs_phys_prismatic_max_motor_force, gs_phys_prismatic_joint, b3_prismaticJoint,
      "max_motor_force", b3PrismaticJoint_GetMaxMotorForce)
GET_F(gs_phys_prismatic_motor_force, gs_phys_prismatic_joint, b3_prismaticJoint, "motor_force",
      b3PrismaticJoint_GetMotorForce)
GET_F(gs_phys_prismatic_translation, gs_phys_prismatic_joint, b3_prismaticJoint, "translation",
      b3PrismaticJoint_GetTranslation)
GET_F(gs_phys_prismatic_speed, gs_phys_prismatic_joint, b3_prismaticJoint, "speed",
      b3PrismaticJoint_GetSpeed)

SET_B(gs_phys_revolute_enable_spring, gs_phys_revolute_joint, b3_revoluteJoint, "enable_spring",
      b3RevoluteJoint_EnableSpring)
GET_B(gs_phys_revolute_spring_enabled, gs_phys_revolute_joint, b3_revoluteJoint, "spring_enabled",
      b3RevoluteJoint_IsSpringEnabled)
SET_F(gs_phys_revolute_set_spring_hertz, gs_phys_revolute_joint, b3_revoluteJoint,
      "set_spring_hertz", b3RevoluteJoint_SetSpringHertz)
GET_F(gs_phys_revolute_spring_hertz, gs_phys_revolute_joint, b3_revoluteJoint, "spring_hertz",
      b3RevoluteJoint_GetSpringHertz)
SET_F(gs_phys_revolute_set_spring_damping_ratio, gs_phys_revolute_joint, b3_revoluteJoint,
      "set_spring_damping_ratio", b3RevoluteJoint_SetSpringDampingRatio)
GET_F(gs_phys_revolute_spring_damping_ratio, gs_phys_revolute_joint, b3_revoluteJoint,
      "spring_damping_ratio", b3RevoluteJoint_GetSpringDampingRatio)
SET_F(gs_phys_revolute_set_target_angle, gs_phys_revolute_joint, b3_revoluteJoint,
      "set_target_angle", b3RevoluteJoint_SetTargetAngle)
GET_F(gs_phys_revolute_target_angle, gs_phys_revolute_joint, b3_revoluteJoint, "target_angle",
      b3RevoluteJoint_GetTargetAngle)
GET_F(gs_phys_revolute_angle, gs_phys_revolute_joint, b3_revoluteJoint, "angle",
      b3RevoluteJoint_GetAngle)
SET_B(gs_phys_revolute_enable_limit, gs_phys_revolute_joint, b3_revoluteJoint, "enable_limit",
      b3RevoluteJoint_EnableLimit)
GET_B(gs_phys_revolute_limit_enabled, gs_phys_revolute_joint, b3_revoluteJoint, "limit_enabled",
      b3RevoluteJoint_IsLimitEnabled)
GET_F(gs_phys_revolute_lower_limit, gs_phys_revolute_joint, b3_revoluteJoint, "lower_limit",
      b3RevoluteJoint_GetLowerLimit)
GET_F(gs_phys_revolute_upper_limit, gs_phys_revolute_joint, b3_revoluteJoint, "upper_limit",
      b3RevoluteJoint_GetUpperLimit)
SET_RANGE(gs_phys_revolute_set_limits, gs_phys_revolute_joint, b3_revoluteJoint, "set_limits",
          b3RevoluteJoint_SetLimits)
SET_B(gs_phys_revolute_enable_motor, gs_phys_revolute_joint, b3_revoluteJoint, "enable_motor",
      b3RevoluteJoint_EnableMotor)
GET_B(gs_phys_revolute_motor_enabled, gs_phys_revolute_joint, b3_revoluteJoint, "motor_enabled",
      b3RevoluteJoint_IsMotorEnabled)
SET_F(gs_phys_revolute_set_motor_speed, gs_phys_revolute_joint, b3_revoluteJoint, "set_motor_speed",
      b3RevoluteJoint_SetMotorSpeed)
GET_F(gs_phys_revolute_motor_speed, gs_phys_revolute_joint, b3_revoluteJoint, "motor_speed",
      b3RevoluteJoint_GetMotorSpeed)
GET_F(gs_phys_revolute_motor_torque, gs_phys_revolute_joint, b3_revoluteJoint, "motor_torque",
      b3RevoluteJoint_GetMotorTorque)
SET_F(gs_phys_revolute_set_max_motor_torque, gs_phys_revolute_joint, b3_revoluteJoint,
      "set_max_motor_torque", b3RevoluteJoint_SetMaxMotorTorque)
GET_F(gs_phys_revolute_max_motor_torque, gs_phys_revolute_joint, b3_revoluteJoint,
      "max_motor_torque", b3RevoluteJoint_GetMaxMotorTorque)

SET_B(gs_phys_spherical_enable_cone_limit, gs_phys_spherical_joint, b3_sphericalJoint,
      "enable_cone_limit", b3SphericalJoint_EnableConeLimit)
GET_B(gs_phys_spherical_cone_limit_enabled, gs_phys_spherical_joint, b3_sphericalJoint,
      "cone_limit_enabled", b3SphericalJoint_IsConeLimitEnabled)
GET_F(gs_phys_spherical_cone_limit, gs_phys_spherical_joint, b3_sphericalJoint, "cone_limit",
      b3SphericalJoint_GetConeLimit)
SET_F(gs_phys_spherical_set_cone_limit, gs_phys_spherical_joint, b3_sphericalJoint,
      "set_cone_limit", b3SphericalJoint_SetConeLimit)
GET_F(gs_phys_spherical_cone_angle, gs_phys_spherical_joint, b3_sphericalJoint, "cone_angle",
      b3SphericalJoint_GetConeAngle)
SET_B(gs_phys_spherical_enable_twist_limit, gs_phys_spherical_joint, b3_sphericalJoint,
      "enable_twist_limit", b3SphericalJoint_EnableTwistLimit)
GET_B(gs_phys_spherical_twist_limit_enabled, gs_phys_spherical_joint, b3_sphericalJoint,
      "twist_limit_enabled", b3SphericalJoint_IsTwistLimitEnabled)
GET_F(gs_phys_spherical_lower_twist_limit, gs_phys_spherical_joint, b3_sphericalJoint,
      "lower_twist_limit", b3SphericalJoint_GetLowerTwistLimit)
GET_F(gs_phys_spherical_upper_twist_limit, gs_phys_spherical_joint, b3_sphericalJoint,
      "upper_twist_limit", b3SphericalJoint_GetUpperTwistLimit)
SET_RANGE(gs_phys_spherical_set_twist_limits, gs_phys_spherical_joint, b3_sphericalJoint,
          "set_twist_limits", b3SphericalJoint_SetTwistLimits)
GET_F(gs_phys_spherical_twist_angle, gs_phys_spherical_joint, b3_sphericalJoint, "twist_angle",
      b3SphericalJoint_GetTwistAngle)
SET_B(gs_phys_spherical_enable_spring, gs_phys_spherical_joint, b3_sphericalJoint, "enable_spring",
      b3SphericalJoint_EnableSpring)
GET_B(gs_phys_spherical_spring_enabled, gs_phys_spherical_joint, b3_sphericalJoint,
      "spring_enabled", b3SphericalJoint_IsSpringEnabled)
SET_F(gs_phys_spherical_set_spring_hertz, gs_phys_spherical_joint, b3_sphericalJoint,
      "set_spring_hertz", b3SphericalJoint_SetSpringHertz)
GET_F(gs_phys_spherical_spring_hertz, gs_phys_spherical_joint, b3_sphericalJoint, "spring_hertz",
      b3SphericalJoint_GetSpringHertz)
SET_F(gs_phys_spherical_set_spring_damping_ratio, gs_phys_spherical_joint, b3_sphericalJoint,
      "set_spring_damping_ratio", b3SphericalJoint_SetSpringDampingRatio)
GET_F(gs_phys_spherical_spring_damping_ratio, gs_phys_spherical_joint, b3_sphericalJoint,
      "spring_damping_ratio", b3SphericalJoint_GetSpringDampingRatio)
SET_B(gs_phys_spherical_enable_motor, gs_phys_spherical_joint, b3_sphericalJoint, "enable_motor",
      b3SphericalJoint_EnableMotor)
GET_B(gs_phys_spherical_motor_enabled, gs_phys_spherical_joint, b3_sphericalJoint, "motor_enabled",
      b3SphericalJoint_IsMotorEnabled)
SET_V(gs_phys_spherical_set_motor_velocity, gs_phys_spherical_joint, b3_sphericalJoint,
      "set_motor_velocity", b3SphericalJoint_SetMotorVelocity)
GET_V(gs_phys_spherical_motor_velocity, gs_phys_spherical_joint, b3_sphericalJoint,
      "motor_velocity", b3SphericalJoint_GetMotorVelocity)
GET_V(gs_phys_spherical_motor_torque, gs_phys_spherical_joint, b3_sphericalJoint, "motor_torque",
      b3SphericalJoint_GetMotorTorque)
SET_F(gs_phys_spherical_set_max_motor_torque, gs_phys_spherical_joint, b3_sphericalJoint,
      "set_max_motor_torque", b3SphericalJoint_SetMaxMotorTorque)
GET_F(gs_phys_spherical_max_motor_torque, gs_phys_spherical_joint, b3_sphericalJoint,
      "max_motor_torque", b3SphericalJoint_GetMaxMotorTorque)

void gs_phys_spherical_set_target_rotation(gs_phys_spherical_joint j, gs_phys_quat rotation) {
    PHYS_KIND(j, b3_sphericalJoint, "set_target_rotation", );
    b3SphericalJoint_SetTargetRotation(id, b3q(rotation));
}

gs_phys_quat gs_phys_spherical_target_rotation(gs_phys_spherical_joint j) {
    gs_phys_quat identity = { 0, 0, 0, 1 };
    PHYS_KIND(j, b3_sphericalJoint, "target_rotation", identity);
    return gsq(b3SphericalJoint_GetTargetRotation(id));
}

SET_F(gs_phys_weld_set_linear_hertz, gs_phys_weld_joint, b3_weldJoint, "set_linear_hertz",
      b3WeldJoint_SetLinearHertz)
GET_F(gs_phys_weld_linear_hertz, gs_phys_weld_joint, b3_weldJoint, "linear_hertz",
      b3WeldJoint_GetLinearHertz)
SET_F(gs_phys_weld_set_linear_damping_ratio, gs_phys_weld_joint, b3_weldJoint,
      "set_linear_damping_ratio", b3WeldJoint_SetLinearDampingRatio)
GET_F(gs_phys_weld_linear_damping_ratio, gs_phys_weld_joint, b3_weldJoint, "linear_damping_ratio",
      b3WeldJoint_GetLinearDampingRatio)
SET_F(gs_phys_weld_set_angular_hertz, gs_phys_weld_joint, b3_weldJoint, "set_angular_hertz",
      b3WeldJoint_SetAngularHertz)
GET_F(gs_phys_weld_angular_hertz, gs_phys_weld_joint, b3_weldJoint, "angular_hertz",
      b3WeldJoint_GetAngularHertz)
SET_F(gs_phys_weld_set_angular_damping_ratio, gs_phys_weld_joint, b3_weldJoint,
      "set_angular_damping_ratio", b3WeldJoint_SetAngularDampingRatio)
GET_F(gs_phys_weld_angular_damping_ratio, gs_phys_weld_joint, b3_weldJoint, "angular_damping_ratio",
      b3WeldJoint_GetAngularDampingRatio)

SET_B(gs_phys_wheel_enable_suspension, gs_phys_wheel_joint, b3_wheelJoint, "enable_suspension",
      b3WheelJoint_EnableSuspension)
GET_B(gs_phys_wheel_suspension_enabled, gs_phys_wheel_joint, b3_wheelJoint, "suspension_enabled",
      b3WheelJoint_IsSuspensionEnabled)
SET_F(gs_phys_wheel_set_suspension_hertz, gs_phys_wheel_joint, b3_wheelJoint,
      "set_suspension_hertz", b3WheelJoint_SetSuspensionHertz)
GET_F(gs_phys_wheel_suspension_hertz, gs_phys_wheel_joint, b3_wheelJoint, "suspension_hertz",
      b3WheelJoint_GetSuspensionHertz)
SET_F(gs_phys_wheel_set_suspension_damping_ratio, gs_phys_wheel_joint, b3_wheelJoint,
      "set_suspension_damping_ratio", b3WheelJoint_SetSuspensionDampingRatio)
GET_F(gs_phys_wheel_suspension_damping_ratio, gs_phys_wheel_joint, b3_wheelJoint,
      "suspension_damping_ratio", b3WheelJoint_GetSuspensionDampingRatio)
SET_B(gs_phys_wheel_enable_suspension_limit, gs_phys_wheel_joint, b3_wheelJoint,
      "enable_suspension_limit", b3WheelJoint_EnableSuspensionLimit)
GET_B(gs_phys_wheel_suspension_limit_enabled, gs_phys_wheel_joint, b3_wheelJoint,
      "suspension_limit_enabled", b3WheelJoint_IsSuspensionLimitEnabled)
GET_F(gs_phys_wheel_lower_suspension_limit, gs_phys_wheel_joint, b3_wheelJoint,
      "lower_suspension_limit", b3WheelJoint_GetLowerSuspensionLimit)
GET_F(gs_phys_wheel_upper_suspension_limit, gs_phys_wheel_joint, b3_wheelJoint,
      "upper_suspension_limit", b3WheelJoint_GetUpperSuspensionLimit)
SET_RANGE(gs_phys_wheel_set_suspension_limits, gs_phys_wheel_joint, b3_wheelJoint,
          "set_suspension_limits", b3WheelJoint_SetSuspensionLimits)
SET_B(gs_phys_wheel_enable_spin_motor, gs_phys_wheel_joint, b3_wheelJoint, "enable_spin_motor",
      b3WheelJoint_EnableSpinMotor)
GET_B(gs_phys_wheel_spin_motor_enabled, gs_phys_wheel_joint, b3_wheelJoint, "spin_motor_enabled",
      b3WheelJoint_IsSpinMotorEnabled)
SET_F(gs_phys_wheel_set_spin_motor_speed, gs_phys_wheel_joint, b3_wheelJoint,
      "set_spin_motor_speed", b3WheelJoint_SetSpinMotorSpeed)
GET_F(gs_phys_wheel_spin_motor_speed, gs_phys_wheel_joint, b3_wheelJoint, "spin_motor_speed",
      b3WheelJoint_GetSpinMotorSpeed)
SET_F(gs_phys_wheel_set_max_spin_torque, gs_phys_wheel_joint, b3_wheelJoint, "set_max_spin_torque",
      b3WheelJoint_SetMaxSpinTorque)
GET_F(gs_phys_wheel_max_spin_torque, gs_phys_wheel_joint, b3_wheelJoint, "max_spin_torque",
      b3WheelJoint_GetMaxSpinTorque)
GET_F(gs_phys_wheel_spin_speed, gs_phys_wheel_joint, b3_wheelJoint, "spin_speed",
      b3WheelJoint_GetSpinSpeed)
GET_F(gs_phys_wheel_spin_torque, gs_phys_wheel_joint, b3_wheelJoint, "spin_torque",
      b3WheelJoint_GetSpinTorque)
SET_B(gs_phys_wheel_enable_steering, gs_phys_wheel_joint, b3_wheelJoint, "enable_steering",
      b3WheelJoint_EnableSteering)
GET_B(gs_phys_wheel_steering_enabled, gs_phys_wheel_joint, b3_wheelJoint, "steering_enabled",
      b3WheelJoint_IsSteeringEnabled)
SET_F(gs_phys_wheel_set_steering_hertz, gs_phys_wheel_joint, b3_wheelJoint, "set_steering_hertz",
      b3WheelJoint_SetSteeringHertz)
GET_F(gs_phys_wheel_steering_hertz, gs_phys_wheel_joint, b3_wheelJoint, "steering_hertz",
      b3WheelJoint_GetSteeringHertz)
SET_F(gs_phys_wheel_set_steering_damping_ratio, gs_phys_wheel_joint, b3_wheelJoint,
      "set_steering_damping_ratio", b3WheelJoint_SetSteeringDampingRatio)
GET_F(gs_phys_wheel_steering_damping_ratio, gs_phys_wheel_joint, b3_wheelJoint,
      "steering_damping_ratio", b3WheelJoint_GetSteeringDampingRatio)
SET_F(gs_phys_wheel_set_max_steering_torque, gs_phys_wheel_joint, b3_wheelJoint,
      "set_max_steering_torque", b3WheelJoint_SetMaxSteeringTorque)
GET_F(gs_phys_wheel_max_steering_torque, gs_phys_wheel_joint, b3_wheelJoint, "max_steering_torque",
      b3WheelJoint_GetMaxSteeringTorque)
SET_B(gs_phys_wheel_enable_steering_limit, gs_phys_wheel_joint, b3_wheelJoint,
      "enable_steering_limit", b3WheelJoint_EnableSteeringLimit)
GET_B(gs_phys_wheel_steering_limit_enabled, gs_phys_wheel_joint, b3_wheelJoint,
      "steering_limit_enabled", b3WheelJoint_IsSteeringLimitEnabled)
GET_F(gs_phys_wheel_lower_steering_limit, gs_phys_wheel_joint, b3_wheelJoint,
      "lower_steering_limit", b3WheelJoint_GetLowerSteeringLimit)
GET_F(gs_phys_wheel_upper_steering_limit, gs_phys_wheel_joint, b3_wheelJoint,
      "upper_steering_limit", b3WheelJoint_GetUpperSteeringLimit)
SET_RANGE(gs_phys_wheel_set_steering_limits, gs_phys_wheel_joint, b3_wheelJoint,
          "set_steering_limits", b3WheelJoint_SetSteeringLimits)
SET_F(gs_phys_wheel_set_target_steering_angle, gs_phys_wheel_joint, b3_wheelJoint,
      "set_target_steering_angle", b3WheelJoint_SetTargetSteeringAngle)
GET_F(gs_phys_wheel_target_steering_angle, gs_phys_wheel_joint, b3_wheelJoint,
      "target_steering_angle", b3WheelJoint_GetTargetSteeringAngle)
GET_F(gs_phys_wheel_steering_angle, gs_phys_wheel_joint, b3_wheelJoint, "steering_angle",
      b3WheelJoint_GetSteeringAngle)
GET_F(gs_phys_wheel_steering_torque, gs_phys_wheel_joint, b3_wheelJoint, "steering_torque",
      b3WheelJoint_GetSteeringTorque)
