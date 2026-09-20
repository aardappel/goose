/* Worlds: stepping, settings, the events of the last step and the queries,
   whose Box3D callbacks collect into the slices the program provides. */

#include "physics_internal.h"

void gs_phys_world_step(gs_phys_world w, float time_step, int64_t sub_steps) {
    PHYS_WORLD(w, "step", );
    if (!(time_step >= 0.0f) || time_step > FLT_MAX) {
        phys_misuse("physics::step: a time step of %g", (double)time_step);
        return;
    }
    if (sub_steps < 1 || sub_steps > 1000) {
        phys_misuse("physics::step: %lld sub-steps", (long long)sub_steps);
        return;
    }
    b3World_Step(id, time_step, (int)sub_steps);
}

gs_phys_aabb gs_phys_world_bounds(gs_phys_world w) {
    gs_phys_aabb none;
    memset(&none, 0, sizeof none);
    PHYS_WORLD(w, "bounds", none);
    return gsbox(b3World_GetBounds(id));
}

void gs_phys_world_enable_sleeping(gs_phys_world w, uint8_t flag) {
    PHYS_WORLD(w, "enable_sleeping", );
    b3World_EnableSleeping(id, flag);
}

uint8_t gs_phys_world_sleeping_enabled(gs_phys_world w) {
    PHYS_WORLD(w, "sleeping_enabled", 0);
    return b3World_IsSleepingEnabled(id);
}

void gs_phys_world_enable_continuous(gs_phys_world w, uint8_t flag) {
    PHYS_WORLD(w, "enable_continuous", );
    b3World_EnableContinuous(id, flag);
}

uint8_t gs_phys_world_continuous_enabled(gs_phys_world w) {
    PHYS_WORLD(w, "continuous_enabled", 0);
    return b3World_IsContinuousEnabled(id);
}

void gs_phys_world_set_restitution_threshold(gs_phys_world w, float value) {
    PHYS_WORLD(w, "set_restitution_threshold", );
    b3World_SetRestitutionThreshold(id, value);
}

float gs_phys_world_restitution_threshold(gs_phys_world w) {
    PHYS_WORLD(w, "restitution_threshold", 0.0f);
    return b3World_GetRestitutionThreshold(id);
}

void gs_phys_world_set_hit_event_threshold(gs_phys_world w, float value) {
    PHYS_WORLD(w, "set_hit_event_threshold", );
    b3World_SetHitEventThreshold(id, value);
}

float gs_phys_world_hit_event_threshold(gs_phys_world w) {
    PHYS_WORLD(w, "hit_event_threshold", 0.0f);
    return b3World_GetHitEventThreshold(id);
}

void gs_phys_world_set_gravity(gs_phys_world w, gs_phys_float3 gravity) {
    PHYS_WORLD(w, "set_gravity", );
    b3World_SetGravity(id, b3v3(gravity));
}

gs_phys_float3 gs_phys_world_gravity(gs_phys_world w) {
    gs_phys_float3 none = { 0, 0, 0 };
    PHYS_WORLD(w, "gravity", none);
    return gsf3(b3World_GetGravity(id));
}

void gs_phys_world_explode(gs_phys_world w, gs_phys_explosion_def def) {
    PHYS_WORLD(w, "explode", );
    b3ExplosionDef d = b3DefaultExplosionDef();
    d.maskBits = def.mask_bits;
    d.position = b3v3(def.position);
    d.radius = def.radius;
    d.falloff = def.falloff;
    d.impulsePerArea = def.impulse_per_area;
    b3World_Explode(id, &d);
}

void gs_phys_world_set_contact_tuning(gs_phys_world w, float hertz, float damping_ratio,
                                      float push_speed) {
    PHYS_WORLD(w, "set_contact_tuning", );
    b3World_SetContactTuning(id, hertz, damping_ratio, push_speed);
}

void gs_phys_world_set_contact_recycle_distance(gs_phys_world w, float distance) {
    PHYS_WORLD(w, "set_contact_recycle_distance", );
    b3World_SetContactRecycleDistance(id, distance);
}

float gs_phys_world_contact_recycle_distance(gs_phys_world w) {
    PHYS_WORLD(w, "contact_recycle_distance", 0.0f);
    return b3World_GetContactRecycleDistance(id);
}

void gs_phys_world_set_maximum_linear_speed(gs_phys_world w, float speed) {
    PHYS_WORLD(w, "set_maximum_linear_speed", );
    b3World_SetMaximumLinearSpeed(id, speed);
}

float gs_phys_world_maximum_linear_speed(gs_phys_world w) {
    PHYS_WORLD(w, "maximum_linear_speed", 0.0f);
    return b3World_GetMaximumLinearSpeed(id);
}

void gs_phys_world_enable_warm_starting(gs_phys_world w, uint8_t flag) {
    PHYS_WORLD(w, "enable_warm_starting", );
    b3World_EnableWarmStarting(id, flag);
}

uint8_t gs_phys_world_warm_starting_enabled(gs_phys_world w) {
    PHYS_WORLD(w, "warm_starting_enabled", 0);
    return b3World_IsWarmStartingEnabled(id);
}

int64_t gs_phys_world_awake_body_count(gs_phys_world w) {
    PHYS_WORLD(w, "awake_body_count", 0);
    return b3World_GetAwakeBodyCount(id);
}

gs_phys_profile gs_phys_world_profile(gs_phys_world w) {
    gs_phys_profile r;
    memset(&r, 0, sizeof r);
    PHYS_WORLD(w, "profile", r);
    b3Profile p = b3World_GetProfile(id);
    r.step = p.step;
    r.pairs = p.pairs;
    r.collide = p.collide;
    r.solve = p.solve;
    r.solver_setup = p.solverSetup;
    r.constraints = p.constraints;
    r.prepare_constraints = p.prepareConstraints;
    r.integrate_velocities = p.integrateVelocities;
    r.warm_start = p.warmStart;
    r.solve_impulses = p.solveImpulses;
    r.integrate_positions = p.integratePositions;
    r.relax_impulses = p.relaxImpulses;
    r.apply_restitution = p.applyRestitution;
    r.store_impulses = p.storeImpulses;
    r.split_islands = p.splitIslands;
    r.transforms = p.transforms;
    r.sensor_hits = p.sensorHits;
    r.joint_events = p.jointEvents;
    r.hit_events = p.hitEvents;
    r.refit = p.refit;
    r.bullets = p.bullets;
    r.sleep_islands = p.sleepIslands;
    r.sensors = p.sensors;
    return r;
}

gs_phys_counters gs_phys_world_counters(gs_phys_world w) {
    gs_phys_counters r;
    memset(&r, 0, sizeof r);
    PHYS_WORLD(w, "counters", r);
    b3Counters c = b3World_GetCounters(id);
    r.body_count = c.bodyCount;
    r.shape_count = c.shapeCount;
    r.contact_count = c.contactCount;
    r.joint_count = c.jointCount;
    r.island_count = c.islandCount;
    r.stack_used = c.stackUsed;
    r.arena_capacity = c.arenaCapacity;
    r.static_tree_height = c.staticTreeHeight;
    r.tree_height = c.treeHeight;
    r.sat_call_count = c.satCallCount;
    r.sat_cache_hit_count = c.satCacheHitCount;
    r.byte_count = c.byteCount;
    r.task_count = c.taskCount;
    for (int i = 0; i < 24; i++) r.color_counts[i] = c.colorCounts[i];
    for (int i = 0; i < 8 && i < B3_CONTACT_MANIFOLD_COUNT_BUCKETS; i++)
        r.manifold_counts[i] = c.manifoldCounts[i];
    r.awake_contact_count = c.awakeContactCount;
    r.recycled_contact_count = c.recycledContactCount;
    r.distance_iterations = c.distanceIterations;
    r.push_back_iterations = c.pushBackIterations;
    r.root_iterations = c.rootIterations;
    return r;
}

gs_phys_capacity gs_phys_world_max_capacity(gs_phys_world w) {
    gs_phys_capacity r;
    memset(&r, 0, sizeof r);
    PHYS_WORLD(w, "max_capacity", r);
    b3Capacity c = b3World_GetMaxCapacity(id);
    r.static_shapes = c.staticShapeCount;
    r.dynamic_shapes = c.dynamicShapeCount;
    r.static_bodies = c.staticBodyCount;
    r.dynamic_bodies = c.dynamicBodyCount;
    r.contacts = c.contactCount;
    return r;
}

void gs_phys_world_set_user_data(gs_phys_world w, uint64_t data) {
    PHYS_WORLD(w, "set_user_data", );
    b3World_SetUserData(id, (void *)(uintptr_t)data);
}

uint64_t gs_phys_world_user_data(gs_phys_world w) {
    PHYS_WORLD(w, "user_data", 0);
    return (uint64_t)(uintptr_t)b3World_GetUserData(id);
}

void gs_phys_world_set_worker_count(gs_phys_world w, int64_t count) {
    PHYS_WORLD(w, "set_worker_count", );
    b3World_SetWorkerCount(id, phys_workers(count));
}

int64_t gs_phys_world_worker_count(gs_phys_world w) {
    PHYS_WORLD(w, "worker_count", 0);
    return b3World_GetWorkerCount(id);
}

/* --- events --------------------------------------------------------------- */

int64_t gs_phys_world_body_move_events(gs_phys_world w, gs_phys_body_move_event_slice out) {
    PHYS_WORLD(w, "body_move_events", 0);
    b3BodyEvents e = b3World_GetBodyEvents(id);
    for (int64_t i = 0; i < e.moveCount && i < out.len; i++) {
        const b3BodyMoveEvent *m = &e.moveEvents[i];
        gs_phys_body_move_event *o = &out.data[i];
        o->transform = gsxf(m->transform);
        o->body = phys_body_of(m->bodyId);
        o->user_data = (uint64_t)(uintptr_t)m->userData;
        o->fell_asleep = m->fellAsleep;
    }
    return e.moveCount;
}

int64_t gs_phys_world_sensor_begin_events(gs_phys_world w, gs_phys_sensor_event_slice out) {
    PHYS_WORLD(w, "sensor_begin_events", 0);
    b3SensorEvents e = b3World_GetSensorEvents(id);
    for (int64_t i = 0; i < e.beginCount && i < out.len; i++) {
        out.data[i].sensor = phys_shape_of(e.beginEvents[i].sensorShapeId);
        out.data[i].visitor = phys_shape_of(e.beginEvents[i].visitorShapeId);
    }
    return e.beginCount;
}

int64_t gs_phys_world_sensor_end_events(gs_phys_world w, gs_phys_sensor_event_slice out) {
    PHYS_WORLD(w, "sensor_end_events", 0);
    b3SensorEvents e = b3World_GetSensorEvents(id);
    for (int64_t i = 0; i < e.endCount && i < out.len; i++) {
        out.data[i].sensor = phys_shape_of(e.endEvents[i].sensorShapeId);
        out.data[i].visitor = phys_shape_of(e.endEvents[i].visitorShapeId);
    }
    return e.endCount;
}

int64_t gs_phys_world_contact_begin_events(gs_phys_world w, gs_phys_contact_event_slice out) {
    PHYS_WORLD(w, "contact_begin_events", 0);
    b3ContactEvents e = b3World_GetContactEvents(id);
    for (int64_t i = 0; i < e.beginCount && i < out.len; i++) {
        const b3ContactBeginTouchEvent *b = &e.beginEvents[i];
        out.data[i].shape_a = phys_shape_of(b->shapeIdA);
        out.data[i].shape_b = phys_shape_of(b->shapeIdB);
        out.data[i].contact = phys_contact_of(b->contactId);
    }
    return e.beginCount;
}

int64_t gs_phys_world_contact_end_events(gs_phys_world w, gs_phys_contact_event_slice out) {
    PHYS_WORLD(w, "contact_end_events", 0);
    b3ContactEvents e = b3World_GetContactEvents(id);
    for (int64_t i = 0; i < e.endCount && i < out.len; i++) {
        const b3ContactEndTouchEvent *x = &e.endEvents[i];
        out.data[i].shape_a = phys_shape_of(x->shapeIdA);
        out.data[i].shape_b = phys_shape_of(x->shapeIdB);
        out.data[i].contact = phys_contact_of(x->contactId);
    }
    return e.endCount;
}

int64_t gs_phys_world_contact_hit_events(gs_phys_world w, gs_phys_contact_hit_event_slice out) {
    PHYS_WORLD(w, "contact_hit_events", 0);
    b3ContactEvents e = b3World_GetContactEvents(id);
    for (int64_t i = 0; i < e.hitCount && i < out.len; i++) {
        const b3ContactHitEvent *h = &e.hitEvents[i];
        gs_phys_contact_hit_event *o = &out.data[i];
        o->shape_a = phys_shape_of(h->shapeIdA);
        o->shape_b = phys_shape_of(h->shapeIdB);
        o->contact = phys_contact_of(h->contactId);
        o->point = gsf3(h->point);
        o->normal = gsf3(h->normal);
        o->approach_speed = h->approachSpeed;
        o->material_a = h->userMaterialIdA;
        o->material_b = h->userMaterialIdB;
    }
    return e.hitCount;
}

int64_t gs_phys_world_joint_events(gs_phys_world w, gs_phys_joint_event_slice out) {
    PHYS_WORLD(w, "joint_events", 0);
    b3JointEvents e = b3World_GetJointEvents(id);
    for (int64_t i = 0; i < e.count && i < out.len; i++) {
        out.data[i].joint = phys_joint_of(e.jointEvents[i].jointId);
        out.data[i].user_data = (uint64_t)(uintptr_t)e.jointEvents[i].userData;
    }
    return e.count;
}

/* --- queries -------------------------------------------------------------- */

typedef struct {
    gs_phys_shape_slice out;
    int64_t n;
} phys_shapes_found;

static bool phys_collect_shape(b3ShapeId shape, void *context) {
    phys_shapes_found *f = (phys_shapes_found *)context;
    if (f->n < f->out.len) f->out.data[f->n] = phys_shape_of(shape);
    f->n++;
    return true;
}

int64_t gs_phys_world_overlap_aabb(gs_phys_world w, gs_phys_aabb box, gs_phys_query_filter filter,
                                   gs_phys_shape_slice out) {
    PHYS_WORLD(w, "overlap_aabb", 0);
    phys_shapes_found f = { out, 0 };
    b3World_OverlapAABB(id, b3box(box), b3qfilter(filter), phys_collect_shape, &f);
    return f.n;
}

int64_t gs_phys_world_overlap_shape(gs_phys_world w, gs_phys_float3 origin,
                                    gs_phys_float3_slice points, float radius,
                                    gs_phys_query_filter filter, gs_phys_shape_slice out) {
    PHYS_WORLD(w, "overlap_shape", 0);
    phys_proxy p;
    if (!phys_make_proxy(&p, points, radius, "overlap_shape")) return 0;
    phys_shapes_found f = { out, 0 };
    b3World_OverlapShape(id, b3v3(origin), &p.proxy, b3qfilter(filter), phys_collect_shape, &f);
    return f.n;
}

/* Every hit of a cast, gathered and then sorted nearest first: Box3D hands
   them over in no particular order. */
typedef struct {
    gs_phys_ray_hit *hits;
    int64_t n, cap;
} phys_hits;

static float phys_collect_hit(b3ShapeId shape, b3Pos point, b3Vec3 normal, float fraction,
                              uint64_t material, int triangle, int child, void *context) {
    phys_hits *h = (phys_hits *)context;
    if (h->n == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 16;
        h->hits = (gs_phys_ray_hit *)realloc(h->hits, (size_t)h->cap * sizeof(gs_phys_ray_hit));
    }
    gs_phys_ray_hit *o = &h->hits[h->n++];
    o->shape = phys_shape_of(shape);
    o->point = gsf3(point);
    o->normal = gsf3(normal);
    o->fraction = fraction;
    o->material_id = material;
    o->triangle_index = triangle;
    o->child_index = child;
    o->hit = 1;
    /* Go on without shortening the ray: all of them are wanted. */
    return 1.0f;
}

static int phys_by_fraction(const void *a, const void *b) {
    float fa = ((const gs_phys_ray_hit *)a)->fraction, fb = ((const gs_phys_ray_hit *)b)->fraction;
    if (fa != fb) return fa < fb ? -1 : 1;
    /* Ties in a stable order, for the same output on every platform. */
    uint64_t sa = ((const gs_phys_ray_hit *)a)->shape.id,
             sb = ((const gs_phys_ray_hit *)b)->shape.id;
    return sa < sb ? -1 : sa > sb;
}

static int64_t phys_put_hits(phys_hits *h, gs_phys_ray_hit_slice out) {
    if (h->n > 1) qsort(h->hits, (size_t)h->n, sizeof(gs_phys_ray_hit), phys_by_fraction);
    for (int64_t i = 0; i < h->n && i < out.len; i++) out.data[i] = h->hits[i];
    free(h->hits);
    return h->n;
}

int64_t gs_phys_world_cast_ray(gs_phys_world w, gs_phys_float3 origin, gs_phys_float3 translation,
                               gs_phys_query_filter filter, gs_phys_ray_hit_slice out) {
    PHYS_WORLD(w, "cast_ray", 0);
    phys_hits h = { NULL, 0, 0 };
    b3World_CastRay(id, b3v3(origin), b3v3(translation), b3qfilter(filter), phys_collect_hit, &h);
    return phys_put_hits(&h, out);
}

gs_phys_ray_hit gs_phys_world_cast_ray_closest(gs_phys_world w, gs_phys_float3 origin,
                                               gs_phys_float3 translation,
                                               gs_phys_query_filter filter) {
    gs_phys_ray_hit r;
    memset(&r, 0, sizeof r);
    PHYS_WORLD(w, "cast_ray_closest", r);
    b3RayResult h = b3World_CastRayClosest(id, b3v3(origin), b3v3(translation), b3qfilter(filter));
    if (!h.hit) return r;
    r.shape = phys_shape_of(h.shapeId);
    r.point = gsf3(h.point);
    r.normal = gsf3(h.normal);
    r.fraction = h.fraction;
    r.material_id = h.userMaterialId;
    r.triangle_index = h.triangleIndex;
    r.child_index = h.childIndex;
    r.hit = 1;
    return r;
}

int64_t gs_phys_world_cast_shape(gs_phys_world w, gs_phys_float3 origin,
                                 gs_phys_float3_slice points, float radius,
                                 gs_phys_float3 translation, gs_phys_query_filter filter,
                                 gs_phys_ray_hit_slice out) {
    PHYS_WORLD(w, "cast_shape", 0);
    phys_proxy p;
    if (!phys_make_proxy(&p, points, radius, "cast_shape")) return 0;
    phys_hits h = { NULL, 0, 0 };
    b3World_CastShape(id, b3v3(origin), &p.proxy, b3v3(translation), b3qfilter(filter),
                      phys_collect_hit, &h);
    return phys_put_hits(&h, out);
}

float gs_phys_world_cast_mover(gs_phys_world w, gs_phys_float3 origin, gs_phys_capsule mover,
                               gs_phys_float3 translation, gs_phys_query_filter filter) {
    PHYS_WORLD(w, "cast_mover", 1.0f);
    b3Capsule c = b3capsule(mover);
    return b3World_CastMover(id, b3v3(origin), &c, b3v3(translation), b3qfilter(filter), NULL,
                             NULL);
}

typedef struct {
    gs_phys_plane_hit_slice out;
    int64_t n;
} phys_planes_found;

static bool phys_collect_planes(b3ShapeId shape, const b3PlaneResult *planes, int count,
                                void *context) {
    phys_planes_found *f = (phys_planes_found *)context;
    for (int i = 0; i < count; i++, f->n++) {
        if (f->n >= f->out.len) continue;
        gs_phys_plane_hit *o = &f->out.data[f->n];
        o->shape = phys_shape_of(shape);
        o->plane = gsplane(planes[i].plane);
        o->point = gsf3(planes[i].point);
        o->triangle_index = planes[i].triangleIndex;
        o->child_index = planes[i].childIndex;
        o->material_index = planes[i].materialIndex;
    }
    return true;
}

int64_t gs_phys_world_collide_mover(gs_phys_world w, gs_phys_float3 origin, gs_phys_capsule mover,
                                    gs_phys_query_filter filter, gs_phys_plane_hit_slice out) {
    PHYS_WORLD(w, "collide_mover", 0);
    b3Capsule c = b3capsule(mover);
    phys_planes_found f = { out, 0 };
    b3World_CollideMover(id, b3v3(origin), &c, b3qfilter(filter), phys_collect_planes, &f);
    return f.n;
}
