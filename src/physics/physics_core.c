/* The physics layer's shared parts: errors, the handle checks, the slot
   tables of the geometry it owns, Box3D's defaults in Goose's shapes, worlds
   themselves, and recording and replay. */

#include "physics_internal.h"

#include <math.h>
#include <stdarg.h>

/* --- errors --------------------------------------------------------------- */

static struct {
    char error[1024];
    int64_t misuse;
    bool inited;
} phys;

static void phys_vset(const char *fmt, va_list args) {
    vsnprintf(phys.error, sizeof phys.error, fmt, args);
}

bool phys_fail(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    phys_vset(fmt, args);
    va_end(args);
    return false;
}

bool phys_misuse(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    phys_vset(fmt, args);
    va_end(args);
    /* The Goose side aborts with the latest at the next step; the first few
       are shown as they happen, since a misuse often causes others. */
    if (++phys.misuse <= 8) fprintf(stderr, "physics: %s\n", phys.error);
    return false;
}

/* Box3D checks its own arguments with asserts in a debug build, which by
   default print to stdout and break into the debugger: a misuse here
   instead, and the call goes on, as it does in a release build where the
   asserts are compiled out. The layer checks what would be unsafe itself. */
static int phys_assert(const char *condition, const char *file, int line) {
    const char *base = strrchr(file, '/');
    const char *base2 = strrchr(file, '\\');
    if (base2 > base) base = base2;
    phys_misuse("Box3D: %s (%s:%d)", condition, base ? base + 1 : file, line);
    return 0;
}

static void phys_log(const char *message) {
    fprintf(stderr, "Box3D: %s\n", message);
}

void phys_init(void) {
    if (phys.inited) return;
    b3SetAssertFcn(phys_assert);
    b3SetLogFcn(phys_log);
    phys.inited = true;
}

uint8_t gs_phys_available(void) { return 1; }

int64_t gs_phys_error(gs_phys_bytes out) {
    return phys_copy_text(phys.error, out);
}

int64_t gs_phys_misuse_count(void) { return phys.misuse; }

int64_t phys_copy_text(const char *text, gs_phys_bytes out) {
    int64_t n = text ? (int64_t)strlen(text) : 0;
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, text, (size_t)k);
    return n;
}

const char *phys_cstr(gs_phys_bytes text) {
    static char *buf;
    static int64_t cap;
    if (text.len + 1 > cap) {
        cap = text.len + 1 > 256 ? text.len + 1 : 256;
        buf = (char *)realloc(buf, (size_t)cap);
    }
    if (text.len > 0) memcpy(buf, text.data, (size_t)text.len);
    buf[text.len] = 0;
    return buf;
}

/* --- handles -------------------------------------------------------------- */

bool phys_world_id(gs_phys_world w, const char *fn, b3WorldId *out) {
    *out = b3LoadWorldId(w.id);
    if (w.id && b3World_IsValid(*out)) return true;
    return phys_misuse("physics::%s: %s", fn,
                       w.id ? "a world that was destroyed, or never created" : "a null world");
}

bool phys_body_id(gs_phys_body b, const char *fn, b3BodyId *out) {
    *out = b3LoadBodyId(b.id);
    if (b.id && b3Body_IsValid(*out)) return true;
    return phys_misuse("physics::%s: %s", fn,
                       b.id ? "a body that was destroyed, or never created" : "a null body");
}

bool phys_shape_id(gs_phys_shape s, const char *fn, b3ShapeId *out) {
    *out = b3LoadShapeId(s.id);
    if (s.id && b3Shape_IsValid(*out)) return true;
    return phys_misuse("physics::%s: %s", fn,
                       s.id ? "a shape that was destroyed, or never created" : "a null shape");
}

bool phys_joint_id(gs_phys_joint j, const char *fn, b3JointId *out) {
    *out = b3LoadJointId(j.id);
    if (j.id && b3Joint_IsValid(*out)) return true;
    return phys_misuse("physics::%s: %s", fn,
                       j.id ? "a joint that was destroyed, or never created" : "a null joint");
}

static const char *phys_joint_names[] = { "parallel", "distance", "filter", "motor", "prismatic",
                                          "revolute", "spherical", "weld", "wheel" };

bool phys_joint_kind(gs_phys_joint j, b3JointType kind, const char *fn, b3JointId *out) {
    if (!phys_joint_id(j, fn, out)) return false;
    b3JointType type = b3Joint_GetType(*out);
    if (type == kind) return true;
    return phys_misuse("physics::%s: a %s joint, not a %s joint", fn, phys_joint_names[type],
                       phys_joint_names[kind]);
}

bool phys_contact_id(gs_phys_contact c, const char *fn, b3ContactId *out) {
    out->index1 = c.index1;
    out->world0 = c.world0;
    out->padding = c.reserved;
    out->generation = c.generation;
    if (c.index1 && b3Contact_IsValid(*out)) return true;
    return phys_misuse("physics::%s: %s", fn,
                       c.index1 ? "a contact that ended" : "a null contact");
}

bool phys_make_proxy(phys_proxy *p, gs_phys_float3_slice points, float radius, const char *fn) {
    phys_init();
    if (points.len < 1 || points.len > B3_MAX_SHAPE_CAST_POINTS)
        return phys_misuse("physics::%s: a shape of %lld points (1 to %d)", fn,
                           (long long)points.len, B3_MAX_SHAPE_CAST_POINTS);
    if (!(radius >= 0.0f) || radius > FLT_MAX)
        return phys_misuse("physics::%s: a radius of %g", fn, (double)radius);
    for (int64_t i = 0; i < points.len; i++) p->points[i] = b3v3(points.data[i]);
    p->proxy.points = p->points;
    p->proxy.count = (int)points.len;
    p->proxy.radius = radius;
    return true;
}

/* --- the geometry the layer owns ------------------------------------------ */

#define PHYS_INDEX_BITS 20
#define PHYS_INDEX_MASK ((1u << PHYS_INDEX_BITS) - 1)

typedef struct {
    phys_item *items;
    uint16_t *gens;
    uint8_t *live;              /* 1: a live handle; 2: released, data still used */
    uint32_t *freelist;
    uint32_t count, cap, nfree;
} phys_table;

static phys_table phys_tables[PHYS_KINDS];
static const char *phys_kind_names[PHYS_KINDS] = { "hull", "mesh", "height field", "compound",
                                                   "recording", "player" };

uint32_t phys_add(phys_kind kind, void *data) {
    phys_table *t = &phys_tables[kind];
    uint32_t i;
    if (t->nfree) {
        i = t->freelist[--t->nfree];
    } else {
        if (t->count == t->cap) {
            uint32_t cap = t->cap ? t->cap * 2 : 64;
            if (cap > PHYS_INDEX_MASK) {
                phys_fail("too many %ses", phys_kind_names[kind]);
                return 0;
            }
            t->items = (phys_item *)realloc(t->items, (size_t)cap * sizeof(phys_item));
            t->gens = (uint16_t *)realloc(t->gens, cap * sizeof(uint16_t));
            t->live = (uint8_t *)realloc(t->live, cap);
            t->freelist = (uint32_t *)realloc(t->freelist, cap * sizeof(uint32_t));
            t->cap = cap;
        }
        i = t->count++;
        t->gens[i] = 0;
    }
    t->gens[i] = (uint16_t)(t->gens[i] % 4095 + 1);
    t->live[i] = 1;
    memset(&t->items[i], 0, sizeof(phys_item));
    t->items[i].data = data;
    return ((uint32_t)t->gens[i] << PHYS_INDEX_BITS) | i;
}

static phys_item *phys_find(phys_kind kind, uint32_t id) {
    phys_table *t = &phys_tables[kind];
    uint32_t i = id & PHYS_INDEX_MASK;
    if (!id || i >= t->count || t->live[i] != 1 || t->gens[i] != id >> PHYS_INDEX_BITS)
        return NULL;
    return &t->items[i];
}

bool phys_is_live(phys_kind kind, uint32_t id) { return phys_find(kind, id) != NULL; }

phys_item *phys_get(phys_kind kind, uint32_t id, const char *fn) {
    phys_item *item = phys_find(kind, id);
    if (!item) {
        if (!id) phys_misuse("physics::%s: a null %s", fn, phys_kind_names[kind]);
        else phys_misuse("physics::%s: a %s that was destroyed, or never created", fn,
                         phys_kind_names[kind]);
    }
    return item;
}

uint32_t phys_find_user(phys_kind kind, b3ShapeId shape) {
    phys_table *t = &phys_tables[kind];
    uint64_t stored = b3StoreShapeId(shape);
    for (uint32_t i = 0; i < t->count; i++) {
        if (t->live[i] != 1) continue;
        for (int u = 0; u < t->items[i].nusers; u++) {
            if (t->items[i].users[u] == stored)
                return ((uint32_t)t->gens[i] << PHYS_INDEX_BITS) | i;
        }
    }
    return 0;
}

uint32_t phys_find_data(phys_kind kind, const void *data) {
    phys_table *t = &phys_tables[kind];
    for (uint32_t i = 0; i < t->count; i++) {
        if (t->live[i] == 1 && t->items[i].data == data)
            return ((uint32_t)t->gens[i] << PHYS_INDEX_BITS) | i;
    }
    return 0;
}

static void phys_free_data(phys_kind kind, void *data) {
    switch (kind) {
        case PHYS_HULL: b3DestroyHull((b3HullData *)data); break;
        case PHYS_MESH: b3DestroyMesh((b3MeshData *)data); break;
        case PHYS_HEIGHT_FIELD: b3DestroyHeightField((b3HeightFieldData *)data); break;
        case PHYS_COMPOUND: b3DestroyCompound((b3CompoundData *)data); break;
        case PHYS_RECORDING: b3DestroyRecording((b3Recording *)data); break;
        case PHYS_PLAYER: b3DestroyPlayer((b3RecPlayer *)data); break;
        default: break;
    }
}

static void phys_free_slot(phys_kind kind, uint32_t i) {
    phys_table *t = &phys_tables[kind];
    phys_free_data(kind, t->items[i].data);
    free(t->items[i].users);
    memset(&t->items[i], 0, sizeof(phys_item));
    t->live[i] = 0;
    t->freelist[t->nfree++] = i;
}

void phys_add_user(phys_item *item, b3ShapeId shape) {
    if (item->nusers == item->capusers) {
        item->capusers = item->capusers ? item->capusers * 2 : 4;
        item->users = (uint64_t *)realloc(item->users, (size_t)item->capusers * sizeof(uint64_t));
    }
    item->users[item->nusers++] = b3StoreShapeId(shape);
}

/* Whether the shape stored as `stored` still exists and still uses `data`:
   set_mesh can point a shape at other data, and a shape slot can be reused. */
static bool phys_still_uses(phys_kind kind, uint64_t stored, const void *data) {
    b3ShapeId s = b3LoadShapeId(stored);
    if (!b3Shape_IsValid(s)) return false;
    b3ShapeType type = b3Shape_GetType(s);
    switch (kind) {
        case PHYS_MESH: return type == b3_meshShape && b3Shape_GetMesh(s).data == data;
        case PHYS_HEIGHT_FIELD: return type == b3_heightShape && b3Shape_GetHeightField(s) == data;
        /* Box3D has no getter for a shape's compound, and nothing turns
           another shape into a compound: a live compound shape is the same
           shape as when it was made, since a reused slot has a new
           generation. */
        case PHYS_COMPOUND: return type == b3_compoundShape;
        default: return false;
    }
}

void phys_sweep(void) {
    for (int k = PHYS_MESH; k <= PHYS_COMPOUND; k++) {
        phys_table *t = &phys_tables[k];
        for (uint32_t i = 0; i < t->count; i++) {
            if (t->live[i] != 2) continue;
            phys_item *item = &t->items[i];
            int n = 0;
            for (int u = 0; u < item->nusers; u++) {
                if (phys_still_uses((phys_kind)k, item->users[u], item->data))
                    item->users[n++] = item->users[u];
            }
            item->nusers = n;
            if (!n) phys_free_slot((phys_kind)k, i);
        }
    }
}

void phys_release(phys_kind kind, uint32_t id, const char *fn) {
    if (!phys_get(kind, id, fn)) return;
    phys_table *t = &phys_tables[kind];
    uint32_t i = id & PHYS_INDEX_MASK;
    if (kind == PHYS_MESH || kind == PHYS_HEIGHT_FIELD || kind == PHYS_COMPOUND) {
        /* The handle is gone at once; the data waits for its shapes. */
        t->live[i] = 2;
        phys_sweep();
    } else {
        phys_free_slot(kind, i);
    }
}

/* --- the library ---------------------------------------------------------- */

gs_phys_version gs_phys_version_of(void) {
    b3Version v = b3GetVersion();
    gs_phys_version r = { v.major, v.minor, v.revision };
    return r;
}

int64_t gs_phys_byte_count(void) { return b3GetByteCount(); }

void gs_phys_set_length_units_per_meter(float units) {
    phys_init();
    if (!(units > 0.0f) || units > FLT_MAX) {
        phys_misuse("physics::set_length_units_per_meter: %g units", (double)units);
        return;
    }
    b3SetLengthUnitsPerMeter(units);
}

float gs_phys_length_units_per_meter(void) { return b3GetLengthUnitsPerMeter(); }
int64_t gs_phys_world_count(void) { return b3GetWorldCount(); }
int64_t gs_phys_max_world_count(void) { return b3GetMaxWorldCount(); }

/* --- definitions ---------------------------------------------------------- */

static float mix_geometric(float a, uint64_t ma, float b, uint64_t mb) {
    (void)ma, (void)mb;
    return sqrtf(a * b);
}
static float mix_min(float a, uint64_t ma, float b, uint64_t mb) {
    (void)ma, (void)mb;
    return a < b ? a : b;
}
static float mix_max(float a, uint64_t ma, float b, uint64_t mb) {
    (void)ma, (void)mb;
    return a > b ? a : b;
}
static float mix_average(float a, uint64_t ma, float b, uint64_t mb) {
    (void)ma, (void)mb;
    return 0.5f * (a + b);
}
static float mix_multiply(float a, uint64_t ma, float b, uint64_t mb) {
    (void)ma, (void)mb;
    return a * b;
}

/* The callback for a MIX_* rule: NULL is Box3D's own. False for no rule. */
static bool phys_mix(int64_t rule, b3FrictionCallback **out, const char *fn) {
    switch (rule) {
        case GS_PHYS_MIX_DEFAULT: *out = NULL; return true;
        case GS_PHYS_MIX_GEOMETRIC: *out = mix_geometric; return true;
        case GS_PHYS_MIX_MIN: *out = mix_min; return true;
        case GS_PHYS_MIX_MAX: *out = mix_max; return true;
        case GS_PHYS_MIX_AVERAGE: *out = mix_average; return true;
        case GS_PHYS_MIX_MULTIPLY: *out = mix_multiply; return true;
        default: return phys_misuse("physics::%s: no mixing rule %lld", fn, (long long)rule);
    }
}

gs_phys_world_def gs_phys_default_world_def(void) {
    phys_init();
    b3WorldDef d = b3DefaultWorldDef();
    gs_phys_world_def r;
    memset(&r, 0, sizeof r);
    r.gravity = gsf3(d.gravity);
    r.restitution_threshold = d.restitutionThreshold;
    r.hit_event_threshold = d.hitEventThreshold;
    r.contact_hertz = d.contactHertz;
    r.contact_damping_ratio = d.contactDampingRatio;
    r.contact_speed = d.contactSpeed;
    r.maximum_linear_speed = d.maximumLinearSpeed;
    r.enable_sleep = d.enableSleep;
    r.enable_continuous = d.enableContinuous;
    r.workers = d.workerCount > 1 ? (int32_t)d.workerCount : 1;
    r.friction_mixing = GS_PHYS_MIX_DEFAULT;
    r.restitution_mixing = GS_PHYS_MIX_DEFAULT;
    r.user_data = (uint64_t)(uintptr_t)d.userData;
    r.capacity.static_shapes = d.capacity.staticShapeCount;
    r.capacity.dynamic_shapes = d.capacity.dynamicShapeCount;
    r.capacity.static_bodies = d.capacity.staticBodyCount;
    r.capacity.dynamic_bodies = d.capacity.dynamicBodyCount;
    r.capacity.contacts = d.capacity.contactCount;
    return r;
}

gs_phys_body_def gs_phys_default_body_def(void) {
    phys_init();
    b3BodyDef d = b3DefaultBodyDef();
    gs_phys_body_def r;
    memset(&r, 0, sizeof r);
    r.body_type = d.type;
    r.position = gsf3(d.position);
    r.rotation = gsq(d.rotation);
    r.linear_velocity = gsf3(d.linearVelocity);
    r.angular_velocity = gsf3(d.angularVelocity);
    r.linear_damping = d.linearDamping;
    r.angular_damping = d.angularDamping;
    r.gravity_scale = d.gravityScale;
    r.sleep_threshold = d.sleepThreshold;
    r.safety_factor = d.safetyFactor;
    r.motion_locks.linear_x = d.motionLocks.linearX;
    r.motion_locks.linear_y = d.motionLocks.linearY;
    r.motion_locks.linear_z = d.motionLocks.linearZ;
    r.motion_locks.angular_x = d.motionLocks.angularX;
    r.motion_locks.angular_y = d.motionLocks.angularY;
    r.motion_locks.angular_z = d.motionLocks.angularZ;
    r.enable_sleep = d.enableSleep;
    r.is_awake = d.isAwake;
    r.is_bullet = d.isBullet;
    r.is_enabled = d.isEnabled;
    r.allow_fast_rotation = d.allowFastRotation;
    r.enable_contact_recycling = d.enableContactRecycling;
    r.user_data = (uint64_t)(uintptr_t)d.userData;
    return r;
}

gs_phys_shape_def gs_phys_default_shape_def(void) {
    phys_init();
    b3ShapeDef d = b3DefaultShapeDef();
    gs_phys_shape_def r;
    memset(&r, 0, sizeof r);
    r.material = gsmaterial(d.baseMaterial);
    r.density = d.density;
    r.filter = gsfilter(d.filter);
    r.is_sensor = d.isSensor;
    r.enable_sensor_events = d.enableSensorEvents;
    r.enable_contact_events = d.enableContactEvents;
    r.enable_hit_events = d.enableHitEvents;
    r.invoke_contact_creation = d.invokeContactCreation;
    r.update_body_mass = d.updateBodyMass;
    r.enable_speculative_contact = d.enableSpeculativeContact;
    r.explosion_scale = d.explosionScale;
    r.user_data = (uint64_t)(uintptr_t)d.userData;
    return r;
}

b3ShapeDef phys_shape_def(const gs_phys_shape_def *def) {
    b3ShapeDef d = b3DefaultShapeDef();
    d.baseMaterial = b3material(def->material);
    d.density = def->density;
    d.filter = b3filter(def->filter);
    d.isSensor = def->is_sensor;
    d.enableSensorEvents = def->enable_sensor_events;
    d.enableContactEvents = def->enable_contact_events;
    d.enableHitEvents = def->enable_hit_events;
    d.invokeContactCreation = def->invoke_contact_creation;
    d.updateBodyMass = def->update_body_mass;
    d.enableSpeculativeContact = def->enable_speculative_contact;
    d.explosionScale = def->explosion_scale;
    d.userData = (void *)(uintptr_t)def->user_data;
    return d;
}

gs_phys_surface_material gs_phys_default_surface_material(void) {
    return gsmaterial(b3DefaultSurfaceMaterial());
}
gs_phys_filter gs_phys_default_filter(void) { return gsfilter(b3DefaultFilter()); }
gs_phys_query_filter gs_phys_default_query_filter(void) {
    b3QueryFilter f = b3DefaultQueryFilter();
    gs_phys_query_filter r = { f.categoryBits, f.maskBits };
    return r;
}

gs_phys_explosion_def gs_phys_default_explosion_def(void) {
    b3ExplosionDef d = b3DefaultExplosionDef();
    gs_phys_explosion_def r = { gsf3(d.position), d.radius, d.falloff, d.impulsePerArea,
                                d.maskBits };
    return r;
}

b3JointDef phys_joint_def(const gs_phys_joint_def *def, b3JointDef base) {
    base.bodyIdA = b3LoadBodyId(def->body_a.id);
    base.bodyIdB = b3LoadBodyId(def->body_b.id);
    base.localFrameA = b3xf(def->frame_a);
    base.localFrameB = b3xf(def->frame_b);
    base.collideConnected = def->collide_connected;
    base.forceThreshold = def->force_threshold;
    base.torqueThreshold = def->torque_threshold;
    base.constraintHertz = def->constraint_hertz;
    base.constraintDampingRatio = def->constraint_damping_ratio;
    base.drawScale = def->draw_scale;
    base.userData = (void *)(uintptr_t)def->user_data;
    return base;
}

gs_phys_joint_def phys_joint_def_of(b3JointDef base) {
    gs_phys_joint_def r;
    memset(&r, 0, sizeof r);
    r.body_a = phys_body_of(base.bodyIdA);
    r.body_b = phys_body_of(base.bodyIdB);
    r.frame_a = gsxf(base.localFrameA);
    r.frame_b = gsxf(base.localFrameB);
    r.collide_connected = base.collideConnected;
    r.force_threshold = base.forceThreshold;
    r.torque_threshold = base.torqueThreshold;
    r.constraint_hertz = base.constraintHertz;
    r.constraint_damping_ratio = base.constraintDampingRatio;
    r.draw_scale = base.drawScale;
    r.user_data = (uint64_t)(uintptr_t)base.userData;
    return r;
}

/* --- worlds --------------------------------------------------------------- */

gs_phys_world gs_phys_create_world(gs_phys_world_def def) {
    gs_phys_world none = { 0 };
    phys_init();
    b3FrictionCallback *friction, *restitution;
    if (!phys_mix(def.friction_mixing, &friction, "create_world") ||
        !phys_mix(def.restitution_mixing, &restitution, "create_world"))
        return none;
    if (b3GetWorldCount() >= B3_MAX_WORLDS) {
        phys_fail("too many worlds: Box3D keeps at most %d at once", B3_MAX_WORLDS);
        return none;
    }
    b3WorldDef d = b3DefaultWorldDef();
    d.gravity = b3v3(def.gravity);
    d.restitutionThreshold = def.restitution_threshold;
    d.hitEventThreshold = def.hit_event_threshold;
    d.contactHertz = def.contact_hertz;
    d.contactDampingRatio = def.contact_damping_ratio;
    d.contactSpeed = def.contact_speed;
    d.maximumLinearSpeed = def.maximum_linear_speed;
    d.enableSleep = def.enable_sleep;
    d.enableContinuous = def.enable_continuous;
    d.workerCount = (uint32_t)phys_workers(def.workers);
    d.frictionCallback = friction;
    d.restitutionCallback = (b3RestitutionCallback *)restitution;
    d.userData = (void *)(uintptr_t)def.user_data;
    d.capacity.staticShapeCount = def.capacity.static_shapes;
    d.capacity.dynamicShapeCount = def.capacity.dynamic_shapes;
    d.capacity.staticBodyCount = def.capacity.static_bodies;
    d.capacity.dynamicBodyCount = def.capacity.dynamic_bodies;
    d.capacity.contactCount = def.capacity.contacts;
    return phys_world_of(b3CreateWorld(&d));
}

/* A recording a world is writing into: the world holds a pointer to it, so
   destroying the recording stops the world recording first. */
static b3WorldId phys_recorders[B3_MAX_WORLDS];
static uint32_t phys_recordings_of[B3_MAX_WORLDS];

void gs_phys_world_destroy(gs_phys_world w) {
    PHYS_WORLD(w, "destroy", );
    phys_recordings_of[id.index1 - 1] = 0;
    b3DestroyWorld(id);
    phys_sweep();
}

uint8_t gs_phys_world_is_valid(gs_phys_world w) {
    return w.id && b3World_IsValid(b3LoadWorldId(w.id));
}

void gs_phys_world_set_friction_mixing(gs_phys_world w, int64_t rule) {
    PHYS_WORLD(w, "set_friction_mixing", );
    b3FrictionCallback *f;
    if (phys_mix(rule, &f, "set_friction_mixing")) b3World_SetFrictionCallback(id, f);
}

void gs_phys_world_set_restitution_mixing(gs_phys_world w, int64_t rule) {
    PHYS_WORLD(w, "set_restitution_mixing", );
    b3FrictionCallback *f;
    if (phys_mix(rule, &f, "set_restitution_mixing"))
        b3World_SetRestitutionCallback(id, (b3RestitutionCallback *)f);
}

/* --- recording and replay ------------------------------------------------- */

gs_phys_recording gs_phys_create_recording(int64_t capacity) {
    gs_phys_recording r = { 0 };
    phys_init();
    if (capacity < 0 || capacity > INT32_MAX) {
        phys_misuse("physics::create_recording: a capacity of %lld bytes", (long long)capacity);
        return r;
    }
    r.id = phys_add(PHYS_RECORDING, b3CreateRecording((int)capacity));
    return r;
}

gs_phys_recording gs_phys_load_recording(gs_phys_bytes path) {
    gs_phys_recording r = { 0 };
    phys_init();
    const char *p = phys_cstr(path);
    b3Recording *rec = b3LoadRecordingFromFile(p);
    if (!rec) {
        phys_fail("cannot load a recording from %s", p);
        return r;
    }
    r.id = phys_add(PHYS_RECORDING, rec);
    return r;
}

static void phys_stop_recorder(uint32_t recording) {
    for (int i = 0; i < B3_MAX_WORLDS; i++) {
        if (phys_recordings_of[i] != recording) continue;
        phys_recordings_of[i] = 0;
        if (b3World_IsValid(phys_recorders[i])) b3World_StopRecording(phys_recorders[i]);
    }
}

void gs_phys_recording_destroy(gs_phys_recording r) {
    if (!phys_get(PHYS_RECORDING, r.id, "destroy")) return;
    phys_stop_recorder(r.id);
    phys_release(PHYS_RECORDING, r.id, "destroy");
}

uint8_t gs_phys_recording_is_valid(gs_phys_recording r) {
    return phys_is_live(PHYS_RECORDING, r.id);
}

int64_t gs_phys_recording_size(gs_phys_recording r) {
    phys_item *item = phys_get(PHYS_RECORDING, r.id, "size");
    return item ? b3Recording_GetSize((b3Recording *)item->data) : 0;
}

int64_t gs_phys_recording_bytes(gs_phys_recording r, gs_phys_bytes out) {
    phys_item *item = phys_get(PHYS_RECORDING, r.id, "bytes");
    if (!item) return 0;
    const b3Recording *rec = (const b3Recording *)item->data;
    int64_t n = b3Recording_GetSize(rec);
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, b3Recording_GetData(rec), (size_t)k);
    return n;
}

uint8_t gs_phys_recording_save(gs_phys_recording r, gs_phys_bytes path) {
    phys_item *item = phys_get(PHYS_RECORDING, r.id, "save");
    if (!item) return 0;
    const char *p = phys_cstr(path);
    if (b3SaveRecordingToFile((const b3Recording *)item->data, p)) return 1;
    return phys_fail("cannot save the recording to %s", p);
}

uint8_t gs_phys_recording_validate(gs_phys_recording r, int64_t workers) {
    phys_item *item = phys_get(PHYS_RECORDING, r.id, "validate_replay");
    if (!item) return 0;
    const b3Recording *rec = (const b3Recording *)item->data;
    if (b3Recording_GetSize(rec) == 0) return phys_fail("the recording is empty");
    if (b3ValidateReplay(b3Recording_GetData(rec), b3Recording_GetSize(rec), phys_workers(workers)))
        return 1;
    return phys_fail("the replay did not reproduce the recording");
}

void gs_phys_world_start_recording(gs_phys_world w, gs_phys_recording r) {
    PHYS_WORLD(w, "start_recording", );
    phys_item *item = phys_get(PHYS_RECORDING, r.id, "start_recording");
    if (!item) return;
    int slot = id.index1 - 1;
    if (phys_recordings_of[slot]) {
        phys_misuse("physics::start_recording: the world is already recording "
                    "(stop_recording first)");
        return;
    }
    for (int i = 0; i < B3_MAX_WORLDS; i++) {
        if (phys_recordings_of[i] == r.id && b3World_IsValid(phys_recorders[i])) {
            phys_misuse("physics::start_recording: another world is recording into it");
            return;
        }
    }
    phys_recorders[slot] = id;
    phys_recordings_of[slot] = r.id;
    b3World_StartRecording(id, (b3Recording *)item->data);
}

void gs_phys_world_stop_recording(gs_phys_world w) {
    PHYS_WORLD(w, "stop_recording", );
    int slot = id.index1 - 1;
    if (!phys_recordings_of[slot]) {
        phys_misuse("physics::stop_recording: the world is not recording");
        return;
    }
    phys_recordings_of[slot] = 0;
    b3World_StopRecording(id);
}

gs_phys_player gs_phys_create_player(gs_phys_recording r, int64_t workers) {
    gs_phys_player p = { 0 };
    phys_item *item = phys_get(PHYS_RECORDING, r.id, "create_player");
    if (!item) return p;
    const b3Recording *rec = (const b3Recording *)item->data;
    if (b3GetWorldCount() >= B3_MAX_WORLDS) {
        phys_fail("too many worlds: Box3D keeps at most %d at once", B3_MAX_WORLDS);
        return p;
    }
    b3RecPlayer *player = NULL;
    if (b3Recording_GetSize(rec) > 0)
        player = b3CreatePlayer(b3Recording_GetData(rec), b3Recording_GetSize(rec),
                                phys_workers(workers));
    if (!player) {
        phys_fail("not a complete recording (stop_recording before replaying it)");
        return p;
    }
    p.id = phys_add(PHYS_PLAYER, player);
    return p;
}

#define PHYS_PLAYER_OR(p, fn, fail) \
    phys_item *item = phys_get(PHYS_PLAYER, (p).id, fn); \
    if (!item) return fail; \
    b3RecPlayer *player = (b3RecPlayer *)item->data

void gs_phys_player_destroy(gs_phys_player p) { phys_release(PHYS_PLAYER, p.id, "destroy"); }
uint8_t gs_phys_player_is_valid(gs_phys_player p) { return phys_is_live(PHYS_PLAYER, p.id); }

uint8_t gs_phys_player_step(gs_phys_player p) {
    PHYS_PLAYER_OR(p, "step", 0);
    return b3RecPlayer_StepFrame(player);
}

void gs_phys_player_sub_step(gs_phys_player p) {
    PHYS_PLAYER_OR(p, "sub_step", );
    b3RecPlayer_SubStepFrame(player);
}

uint8_t gs_phys_player_at_pre_step(gs_phys_player p) {
    PHYS_PLAYER_OR(p, "at_pre_step", 0);
    return b3RecPlayer_IsAtPreStep(player);
}

void gs_phys_player_restart(gs_phys_player p) {
    PHYS_PLAYER_OR(p, "restart", );
    b3RecPlayer_Restart(player);
}

void gs_phys_player_seek(gs_phys_player p, int64_t frame) {
    PHYS_PLAYER_OR(p, "seek", );
    int64_t n = b3RecPlayer_GetFrameCount(player);
    if (frame < 0 || frame > n) {
        phys_misuse("physics::seek: frame %lld of a recording of %lld", (long long)frame,
                    (long long)n);
        return;
    }
    b3RecPlayer_SeekFrame(player, (int)frame);
}

gs_phys_world gs_phys_player_world(gs_phys_player p) {
    gs_phys_world none = { 0 };
    PHYS_PLAYER_OR(p, "world", none);
    return phys_world_of(b3RecPlayer_GetWorldId(player));
}

int64_t gs_phys_player_frame(gs_phys_player p) {
    PHYS_PLAYER_OR(p, "frame", 0);
    return b3RecPlayer_GetFrame(player);
}

int64_t gs_phys_player_frame_count(gs_phys_player p) {
    PHYS_PLAYER_OR(p, "frame_count", 0);
    return b3RecPlayer_GetFrameCount(player);
}

uint8_t gs_phys_player_at_end(gs_phys_player p) {
    PHYS_PLAYER_OR(p, "at_end", 0);
    return b3RecPlayer_IsAtEnd(player);
}

uint8_t gs_phys_player_diverged(gs_phys_player p) {
    PHYS_PLAYER_OR(p, "diverged", 0);
    return b3RecPlayer_HasDiverged(player);
}

int64_t gs_phys_player_diverge_frame(gs_phys_player p) {
    PHYS_PLAYER_OR(p, "diverge_frame", -1);
    return b3RecPlayer_GetDivergeFrame(player);
}

int64_t gs_phys_player_body_count(gs_phys_player p) {
    PHYS_PLAYER_OR(p, "body_count", 0);
    return b3RecPlayer_GetBodyCount(player);
}

gs_phys_body gs_phys_player_body(gs_phys_player p, int64_t index) {
    gs_phys_body none = { 0 };
    PHYS_PLAYER_OR(p, "body", none);
    int64_t n = b3RecPlayer_GetBodyCount(player);
    if (index < 0 || index >= n) {
        phys_misuse("physics::body: body %lld of %lld recorded", (long long)index, (long long)n);
        return none;
    }
    return phys_body_of(b3RecPlayer_GetBodyId(player, (int)index));
}

gs_phys_player_info gs_phys_player_info_of(gs_phys_player p) {
    gs_phys_player_info r;
    memset(&r, 0, sizeof r);
    PHYS_PLAYER_OR(p, "info", r);
    b3RecPlayerInfo i = b3RecPlayer_GetInfo(player);
    r.frame_count = i.frameCount;
    r.worker_count = i.workerCount;
    r.time_step = i.timeStep;
    r.sub_step_count = i.subStepCount;
    r.length_scale = i.lengthScale;
    r.bounds = gsbox(i.bounds);
    return r;
}

void gs_phys_player_set_worker_count(gs_phys_player p, int64_t count) {
    PHYS_PLAYER_OR(p, "set_worker_count", );
    b3RecPlayer_SetWorkerCount(player, phys_workers(count));
}
