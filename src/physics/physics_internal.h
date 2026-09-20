/* Shared by the physics layer's own C files in src/physics/, never by a
   program: the conversions between the packed Goose-shaped structs of
   physics_api.h and Box3D's, the handle checks every entry point starts
   with, errors, and the slot tables of the geometry the layer owns. Like the
   gfx layer, it is used from the main thread only (the compiler rejects a
   thread_fn that reaches it); Box3D runs its own worker threads inside a
   step. */

#ifndef GS_PHYS_INTERNAL_H
#define GS_PHYS_INTERNAL_H

#include "box3d/box3d.h"

#include <float.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "physics_api.h"

/* --- errors --------------------------------------------------------------- */

/* Something outside the program's control failed: the reason is kept for
   gs_phys_error. Returns false, for `return phys_fail(...)`. */
bool phys_fail(const char *fmt, ...);
/* The program used the API wrongly: kept, counted and printed, and the
   Goose side aborts on it at the next step or check. Returns false. */
bool phys_misuse(const char *fmt, ...);

/* Routes Box3D's asserts and log into the layer's reporting instead of
   stdout, which a program owns; called before anything reaches Box3D. */
void phys_init(void);

/* --- handles -----------------------------------------------------------------
   The checks every entry point makes before Box3D sees an id: `fn` is the
   Goose name the message gives. Each is false, and a misuse, for a null or
   stale id. */

bool phys_world_id(gs_phys_world w, const char *fn, b3WorldId *out);
bool phys_body_id(gs_phys_body b, const char *fn, b3BodyId *out);
bool phys_shape_id(gs_phys_shape s, const char *fn, b3ShapeId *out);
bool phys_joint_id(gs_phys_joint j, const char *fn, b3JointId *out);
/* A joint that must also be of one kind. */
bool phys_joint_kind(gs_phys_joint j, b3JointType kind, const char *fn, b3JointId *out);
bool phys_contact_id(gs_phys_contact c, const char *fn, b3ContactId *out);

/* The first lines of most entry points: the id in `id`, or return `fail`.
   `fail` may be empty, for a void function. */
#define PHYS_WORLD(w, fn, fail) \
    b3WorldId id; \
    if (!phys_world_id(w, fn, &id)) return fail
#define PHYS_BODY(b, fn, fail) \
    b3BodyId id; \
    if (!phys_body_id(b, fn, &id)) return fail
#define PHYS_SHAPE(s, fn, fail) \
    b3ShapeId id; \
    if (!phys_shape_id(s, fn, &id)) return fail
#define PHYS_JOINT(j, fn, fail) \
    b3JointId id; \
    if (!phys_joint_id(j, fn, &id)) return fail
#define PHYS_KIND(j, kind, fn, fail) \
    b3JointId id; \
    if (!phys_joint_kind((j).joint, kind, fn, &id)) return fail

/* Ids and handles, both ways. Box3D's null ids store as 0. */
static inline gs_phys_world phys_world_of(b3WorldId id) {
    gs_phys_world w = { b3StoreWorldId(id) };
    return w;
}
static inline gs_phys_body phys_body_of(b3BodyId id) {
    gs_phys_body b = { b3StoreBodyId(id) };
    return b;
}
static inline gs_phys_shape phys_shape_of(b3ShapeId id) {
    gs_phys_shape s = { b3StoreShapeId(id) };
    return s;
}
static inline gs_phys_joint phys_joint_of(b3JointId id) {
    gs_phys_joint j = { b3StoreJointId(id) };
    return j;
}
static inline gs_phys_contact phys_contact_of(b3ContactId id) {
    gs_phys_contact c = { id.index1, id.world0, id.padding, id.generation };
    return c;
}

/* --- values ------------------------------------------------------------------
   b3* from the Goose shapes, and gs* back. */

static inline b3Vec3 b3v3(gs_phys_float3 v) {
    b3Vec3 r = { v.x, v.y, v.z };
    return r;
}
static inline gs_phys_float3 gsf3(b3Vec3 v) {
    gs_phys_float3 r = { v.x, v.y, v.z };
    return r;
}
static inline b3Quat b3q(gs_phys_quat q) {
    b3Quat r = { { q.x, q.y, q.z }, q.w };
    return r;
}
static inline gs_phys_quat gsq(b3Quat q) {
    gs_phys_quat r = { q.v.x, q.v.y, q.v.z, q.s };
    return r;
}
static inline b3Transform b3xf(gs_phys_transform t) {
    b3Transform r = { b3v3(t.p), b3q(t.q) };
    return r;
}
static inline gs_phys_transform gsxf(b3Transform t) {
    gs_phys_transform r = { gsf3(t.p), gsq(t.q) };
    return r;
}
static inline b3Matrix3 b3m3(gs_phys_mat3 m) {
    b3Matrix3 r = { b3v3(m.cx), b3v3(m.cy), b3v3(m.cz) };
    return r;
}
static inline gs_phys_mat3 gsm3(b3Matrix3 m) {
    gs_phys_mat3 r = { gsf3(m.cx), gsf3(m.cy), gsf3(m.cz) };
    return r;
}
static inline b3AABB b3box(gs_phys_aabb a) {
    b3AABB r = { b3v3(a.lower), b3v3(a.upper) };
    return r;
}
static inline gs_phys_aabb gsbox(b3AABB a) {
    gs_phys_aabb r = { gsf3(a.lowerBound), gsf3(a.upperBound) };
    return r;
}
static inline b3Plane b3plane(gs_phys_plane p) {
    b3Plane r = { b3v3(p.normal), p.offset };
    return r;
}
static inline gs_phys_plane gsplane(b3Plane p) {
    gs_phys_plane r = { gsf3(p.normal), p.offset };
    return r;
}
static inline b3MassData b3mass(gs_phys_mass_data m) {
    b3MassData r = { m.mass, b3v3(m.center), b3m3(m.inertia) };
    return r;
}
static inline gs_phys_mass_data gsmass(b3MassData m) {
    gs_phys_mass_data r = { m.mass, gsf3(m.center), gsm3(m.inertia) };
    return r;
}
static inline b3Sphere b3sphere(gs_phys_sphere s) {
    b3Sphere r = { b3v3(s.center), s.radius };
    return r;
}
static inline gs_phys_sphere gssphere(b3Sphere s) {
    gs_phys_sphere r = { gsf3(s.center), s.radius };
    return r;
}
static inline b3Capsule b3capsule(gs_phys_capsule c) {
    b3Capsule r = { b3v3(c.center1), b3v3(c.center2), c.radius };
    return r;
}
static inline gs_phys_capsule gscapsule(b3Capsule c) {
    gs_phys_capsule r = { gsf3(c.center1), gsf3(c.center2), c.radius };
    return r;
}
static inline b3SurfaceMaterial b3material(gs_phys_surface_material m) {
    b3SurfaceMaterial r = b3DefaultSurfaceMaterial();
    r.friction = m.friction;
    r.restitution = m.restitution;
    r.rollingResistance = m.rolling_resistance;
    r.tangentVelocity = b3v3(m.tangent_velocity);
    r.userMaterialId = m.user_material_id;
    r.customColor = m.custom_color;
    return r;
}
static inline gs_phys_surface_material gsmaterial(b3SurfaceMaterial m) {
    gs_phys_surface_material r = { m.friction, m.restitution, m.rollingResistance,
                                   gsf3(m.tangentVelocity), m.userMaterialId, m.customColor };
    return r;
}
static inline b3Filter b3filter(gs_phys_filter f) {
    b3Filter r = { f.category_bits, f.mask_bits, f.group_index };
    return r;
}
static inline gs_phys_filter gsfilter(b3Filter f) {
    gs_phys_filter r = { f.categoryBits, f.maskBits, f.groupIndex };
    return r;
}
static inline b3QueryFilter b3qfilter(gs_phys_query_filter f) {
    b3QueryFilter r = b3DefaultQueryFilter();
    r.categoryBits = f.category_bits;
    r.maskBits = f.mask_bits;
    return r;
}
static inline gs_phys_cast_output gscast(b3CastOutput o) {
    gs_phys_cast_output r = { gsf3(o.normal), gsf3(o.point), o.fraction, o.iterations,
                              o.triangleIndex, o.childIndex, o.materialIndex, o.hit };
    return r;
}

/* Each field of a Goose-shaped definition onto Box3D's defaults, so the
   fields the layer does not expose keep Box3D's values. */
b3ShapeDef phys_shape_def(const gs_phys_shape_def *def);
b3JointDef phys_joint_def(const gs_phys_joint_def *def, b3JointDef base);
gs_phys_joint_def phys_joint_def_of(b3JointDef base);

/* A point cloud with a radius for a query or a cast: the points copied out
   of the program's slice, which Box3D reads as naturally aligned b3Vec3s.
   A misuse for none or more than B3_MAX_SHAPE_CAST_POINTS. */
typedef struct {
    b3Vec3 points[B3_MAX_SHAPE_CAST_POINTS];
    b3ShapeProxy proxy;
} phys_proxy;
bool phys_make_proxy(phys_proxy *p, gs_phys_float3_slice points, float radius, const char *fn);

/* A count of worker threads within what Box3D takes: 1 to B3_MAX_WORKERS,
   so a program can ask for as many as the machine has. */
static inline int phys_workers(int64_t n) {
    return n < 1 ? 1 : n > B3_MAX_WORKERS ? B3_MAX_WORKERS : (int)n;
}

/* Box3D's null-terminated text, copied into `out` as far as it fits;
   returns the whole length. */
int64_t phys_copy_text(const char *text, gs_phys_bytes out);
/* The program's text, null-terminated, in a buffer of the layer's: valid
   until the next call. */
const char *phys_cstr(gs_phys_bytes text);

/* A contact's manifolds as gs_phys_manifold rows, as many as fit from
   `*n` on; `*n` counts them all. */
void phys_put_manifolds(b3ContactData data, gs_phys_manifold_slice out, int64_t *n);

/* --- the geometry the layer owns ---------------------------------------------
   Hulls, meshes, height fields and compounds are Box3D allocations, handed to
   the program as slot handles: a slot index in the low 20 bits and a
   generation above it. Box3D copies a hull into each world that uses it,
   but mesh, height field and compound shapes keep pointing at the data they
   were made from: destroying one of those only releases the handle, and the
   data is freed once no shape uses it any more. */

typedef enum { PHYS_HULL, PHYS_MESH, PHYS_HEIGHT_FIELD, PHYS_COMPOUND, PHYS_RECORDING,
               PHYS_PLAYER, PHYS_KINDS } phys_kind;

typedef struct {
    void *data;
    bool released;          /* destroyed by the program, freed when unused */
    uint64_t *users;        /* stored ids of the shapes made from it */
    int nusers, capusers;
} phys_item;

/* A new handle for `data`, or 0 and a failure when the table is full. */
uint32_t phys_add(phys_kind kind, void *data);
/* The item behind a live handle; a misuse naming `fn` otherwise. */
phys_item *phys_get(phys_kind kind, uint32_t id, const char *fn);
/* The live handle holding `data`, or 0: from a shape back to its handle. */
uint32_t phys_find_data(phys_kind kind, const void *data);
/* The live handle a shape was made from, or 0, by the users lists. */
uint32_t phys_find_user(phys_kind kind, b3ShapeId shape);
bool phys_is_live(phys_kind kind, uint32_t id);
/* Releases a handle: frees the data now, or once no shape uses it. */
void phys_release(phys_kind kind, uint32_t id, const char *fn);
/* Notes that a shape now uses the data of `item`. */
void phys_add_user(phys_item *item, b3ShapeId shape);
/* Frees released data no live shape uses any more; called after anything
   that destroys shapes. */
void phys_sweep(void);

#endif
