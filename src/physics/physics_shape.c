/* Shapes: creating each kind on a body, their materials, filters and events,
   their geometry, and the contacts they take part in. */

#include "physics_internal.h"

static gs_phys_float3 phys_zero3(void) {
    gs_phys_float3 r = { 0, 0, 0 };
    return r;
}

static bool phys_finite3(gs_phys_float3 v) {
    return b3IsValidFloat(v.x) && b3IsValidFloat(v.y) && b3IsValidFloat(v.z);
}

/* What every creation checks: the body, and a definition Box3D would
   assert on. */
static bool phys_shape_start(gs_phys_body b, const gs_phys_shape_def *def, const char *fn,
                             b3BodyId *body, b3ShapeDef *d) {
    if (!phys_body_id(b, fn, body)) return false;
    if (!(def->density >= 0.0f) || def->density > FLT_MAX)
        return phys_misuse("physics::%s: a density of %g", fn, (double)def->density);
    if (!(def->material.friction >= 0.0f) || !(def->material.restitution >= 0.0f))
        return phys_misuse("physics::%s: a friction of %g and a restitution of %g", fn,
                           (double)def->material.friction, (double)def->material.restitution);
    *d = phys_shape_def(def);
    return true;
}

static bool phys_static_only(b3BodyId body, const char *fn, const char *what) {
    if (b3Body_GetType(body) == b3_staticBody) return true;
    return phys_misuse("physics::%s: %s shapes go on static bodies only", fn, what);
}

static bool phys_radius_ok(float radius, const char *fn) {
    if (radius >= 0.0f && radius <= FLT_MAX) return true;
    return phys_misuse("physics::%s: a radius of %g", fn, (double)radius);
}

static bool phys_scale_ok(gs_phys_float3 scale, const char *fn) {
    if (phys_finite3(scale) && scale.x != 0.0f && scale.y != 0.0f && scale.z != 0.0f) return true;
    return phys_misuse("physics::%s: a scale of %g, %g, %g", fn, (double)scale.x, (double)scale.y,
                       (double)scale.z);
}

gs_phys_shape gs_phys_create_sphere_shape(gs_phys_body b, gs_phys_shape_def def,
                                          gs_phys_sphere sphere) {
    gs_phys_shape none = { 0 };
    b3BodyId body;
    b3ShapeDef d;
    if (!phys_shape_start(b, &def, "create_sphere_shape", &body, &d)) return none;
    if (!phys_radius_ok(sphere.radius, "create_sphere_shape")) return none;
    b3Sphere s = b3sphere(sphere);
    return phys_shape_of(b3CreateSphereShape(body, &d, &s));
}

gs_phys_shape gs_phys_create_capsule_shape(gs_phys_body b, gs_phys_shape_def def,
                                           gs_phys_capsule capsule) {
    gs_phys_shape none = { 0 };
    b3BodyId body;
    b3ShapeDef d;
    if (!phys_shape_start(b, &def, "create_capsule_shape", &body, &d)) return none;
    if (!phys_radius_ok(capsule.radius, "create_capsule_shape")) return none;
    b3Capsule c = b3capsule(capsule);
    return phys_shape_of(b3CreateCapsuleShape(body, &d, &c));
}

gs_phys_shape gs_phys_create_box_shape(gs_phys_body b, gs_phys_shape_def def,
                                       gs_phys_float3 half_extents, gs_phys_transform frame) {
    gs_phys_shape none = { 0 };
    b3BodyId body;
    b3ShapeDef d;
    if (!phys_shape_start(b, &def, "create_box_shape", &body, &d)) return none;
    if (!phys_finite3(half_extents) || !(half_extents.x > 0.0f) || !(half_extents.y > 0.0f) ||
        !(half_extents.z > 0.0f)) {
        phys_misuse("physics::create_box_shape: half extents of %g, %g, %g", (double)half_extents.x,
                    (double)half_extents.y, (double)half_extents.z);
        return none;
    }
    b3BoxHull box = b3MakeTransformedBoxHull(half_extents.x, half_extents.y, half_extents.z,
                                             b3xf(frame));
    return phys_shape_of(b3CreateHullShape(body, &d, &box.base));
}

gs_phys_shape gs_phys_create_hull_shape(gs_phys_body b, gs_phys_shape_def def, gs_phys_hull hull) {
    gs_phys_shape none = { 0 };
    b3BodyId body;
    b3ShapeDef d;
    if (!phys_shape_start(b, &def, "create_hull_shape", &body, &d)) return none;
    phys_item *h = phys_get(PHYS_HULL, hull.id, "create_hull_shape");
    if (!h) return none;
    return phys_shape_of(b3CreateHullShape(body, &d, (const b3HullData *)h->data));
}

gs_phys_shape gs_phys_create_transformed_hull_shape(gs_phys_body b, gs_phys_shape_def def,
                                                    gs_phys_hull hull, gs_phys_transform transform,
                                                    gs_phys_float3 scale) {
    gs_phys_shape none = { 0 };
    b3BodyId body;
    b3ShapeDef d;
    if (!phys_shape_start(b, &def, "create_transformed_hull_shape", &body, &d)) return none;
    phys_item *h = phys_get(PHYS_HULL, hull.id, "create_transformed_hull_shape");
    if (!h || !phys_scale_ok(scale, "create_transformed_hull_shape")) return none;
    b3ShapeId s = b3CreateTransformedHullShape(body, &d, (const b3HullData *)h->data,
                                               b3xf(transform), b3v3(scale));
    if (B3_IS_NULL(s)) phys_fail("the transformed hull is degenerate");
    return phys_shape_of(s);
}

/* The per-triangle materials of a mesh or height field shape; none means
   the definition's material for every triangle. */
static bool phys_materials(gs_phys_surface_material_slice materials, b3ShapeDef *d,
                           b3SurfaceMaterial **owned, const char *fn) {
    *owned = NULL;
    if (materials.len == 0) return true;
    if (materials.len > 255)
        return phys_misuse("physics::%s: %lld materials (at most 255)", fn,
                           (long long)materials.len);
    *owned = (b3SurfaceMaterial *)malloc((size_t)materials.len * sizeof(b3SurfaceMaterial));
    for (int64_t i = 0; i < materials.len; i++) (*owned)[i] = b3material(materials.data[i]);
    d->materials = *owned;
    d->materialCount = (int)materials.len;
    return true;
}

gs_phys_shape gs_phys_create_mesh_shape(gs_phys_body b, gs_phys_shape_def def, gs_phys_mesh mesh,
                                        gs_phys_float3 scale,
                                        gs_phys_surface_material_slice materials) {
    gs_phys_shape none = { 0 };
    b3BodyId body;
    b3ShapeDef d;
    b3SurfaceMaterial *owned;
    if (!phys_shape_start(b, &def, "create_mesh_shape", &body, &d)) return none;
    phys_item *m = phys_get(PHYS_MESH, mesh.id, "create_mesh_shape");
    if (!m || !phys_scale_ok(scale, "create_mesh_shape") ||
        !phys_materials(materials, &d, &owned, "create_mesh_shape"))
        return none;
    b3ShapeId s = b3CreateMeshShape(body, &d, (const b3MeshData *)m->data, b3v3(scale));
    free(owned);
    if (B3_IS_NON_NULL(s)) phys_add_user(m, s);
    return phys_shape_of(s);
}

gs_phys_shape gs_phys_create_height_field_shape(gs_phys_body b, gs_phys_shape_def def,
                                                gs_phys_height_field height_field,
                                                gs_phys_surface_material_slice materials) {
    gs_phys_shape none = { 0 };
    b3BodyId body;
    b3ShapeDef d;
    b3SurfaceMaterial *owned;
    if (!phys_shape_start(b, &def, "create_height_field_shape", &body, &d)) return none;
    phys_item *h = phys_get(PHYS_HEIGHT_FIELD, height_field.id, "create_height_field_shape");
    if (!h || !phys_static_only(body, "create_height_field_shape", "height field") ||
        !phys_materials(materials, &d, &owned, "create_height_field_shape"))
        return none;
    b3ShapeId s = b3CreateHeightFieldShape(body, &d, (const b3HeightFieldData *)h->data);
    free(owned);
    if (B3_IS_NON_NULL(s)) phys_add_user(h, s);
    return phys_shape_of(s);
}

gs_phys_shape gs_phys_create_compound_shape(gs_phys_body b, gs_phys_shape_def def,
                                            gs_phys_compound compound) {
    gs_phys_shape none = { 0 };
    b3BodyId body;
    b3ShapeDef d;
    if (!phys_shape_start(b, &def, "create_compound_shape", &body, &d)) return none;
    phys_item *c = phys_get(PHYS_COMPOUND, compound.id, "create_compound_shape");
    if (!c || !phys_static_only(body, "create_compound_shape", "compound")) return none;
    b3ShapeId s = b3CreateBakedCompoundShape(body, &d, (const b3CompoundData *)c->data);
    if (B3_IS_NON_NULL(s)) phys_add_user(c, s);
    return phys_shape_of(s);
}

void gs_phys_shape_destroy(gs_phys_shape s, uint8_t update_body_mass) {
    PHYS_SHAPE(s, "destroy", );
    b3DestroyShape(id, update_body_mass);
    phys_sweep();
}

uint8_t gs_phys_shape_is_valid(gs_phys_shape s) {
    return s.id && b3Shape_IsValid(b3LoadShapeId(s.id));
}

int32_t gs_phys_shape_type(gs_phys_shape s) {
    PHYS_SHAPE(s, "shape_type", 0);
    return b3Shape_GetType(id);
}

gs_phys_body gs_phys_shape_body(gs_phys_shape s) {
    gs_phys_body none = { 0 };
    PHYS_SHAPE(s, "body", none);
    return phys_body_of(b3Shape_GetBody(id));
}

gs_phys_world gs_phys_shape_world(gs_phys_shape s) {
    gs_phys_world none = { 0 };
    PHYS_SHAPE(s, "world", none);
    return phys_world_of(b3Shape_GetWorld(id));
}

uint8_t gs_phys_shape_is_sensor(gs_phys_shape s) {
    PHYS_SHAPE(s, "is_sensor", 0);
    return b3Shape_IsSensor(id);
}

void gs_phys_shape_set_name(gs_phys_shape s, gs_phys_bytes name) {
    PHYS_SHAPE(s, "set_name", );
    b3Shape_SetName(id, phys_cstr(name));
}

int64_t gs_phys_shape_name(gs_phys_shape s, gs_phys_bytes out) {
    PHYS_SHAPE(s, "name", 0);
    return phys_copy_text(b3Shape_GetName(id), out);
}

void gs_phys_shape_set_user_data(gs_phys_shape s, uint64_t data) {
    PHYS_SHAPE(s, "set_user_data", );
    b3Shape_SetUserData(id, (void *)(uintptr_t)data);
}

uint64_t gs_phys_shape_user_data(gs_phys_shape s) {
    PHYS_SHAPE(s, "user_data", 0);
    return (uint64_t)(uintptr_t)b3Shape_GetUserData(id);
}

void gs_phys_shape_set_density(gs_phys_shape s, float density, uint8_t update_body_mass) {
    PHYS_SHAPE(s, "set_density", );
    if (!(density >= 0.0f) || density > FLT_MAX) {
        phys_misuse("physics::set_density: a density of %g", (double)density);
        return;
    }
    b3Shape_SetDensity(id, density, update_body_mass);
}

float gs_phys_shape_density(gs_phys_shape s) {
    PHYS_SHAPE(s, "density", 0.0f);
    return b3Shape_GetDensity(id);
}

void gs_phys_shape_set_friction(gs_phys_shape s, float friction) {
    PHYS_SHAPE(s, "set_friction", );
    b3Shape_SetFriction(id, friction);
}

float gs_phys_shape_friction(gs_phys_shape s) {
    PHYS_SHAPE(s, "friction", 0.0f);
    return b3Shape_GetFriction(id);
}

void gs_phys_shape_set_restitution(gs_phys_shape s, float restitution) {
    PHYS_SHAPE(s, "set_restitution", );
    b3Shape_SetRestitution(id, restitution);
}

float gs_phys_shape_restitution(gs_phys_shape s) {
    PHYS_SHAPE(s, "restitution", 0.0f);
    return b3Shape_GetRestitution(id);
}

void gs_phys_shape_set_surface_material(gs_phys_shape s, gs_phys_surface_material material) {
    PHYS_SHAPE(s, "set_surface_material", );
    b3Shape_SetSurfaceMaterial(id, b3material(material));
}

gs_phys_surface_material gs_phys_shape_surface_material(gs_phys_shape s) {
    gs_phys_surface_material none;
    memset(&none, 0, sizeof none);
    PHYS_SHAPE(s, "surface_material", none);
    return gsmaterial(b3Shape_GetSurfaceMaterial(id));
}

int64_t gs_phys_shape_mesh_material_count(gs_phys_shape s) {
    PHYS_SHAPE(s, "mesh_material_count", 0);
    return b3Shape_GetMeshMaterialCount(id);
}

static bool phys_mesh_material_index(b3ShapeId id, int64_t index, const char *fn) {
    int64_t n = b3Shape_GetMeshMaterialCount(id);
    if (index >= 0 && index < n) return true;
    return phys_misuse("physics::%s: material %lld of %lld", fn, (long long)index, (long long)n);
}

void gs_phys_shape_set_mesh_material(gs_phys_shape s, gs_phys_surface_material material,
                                     int64_t index) {
    PHYS_SHAPE(s, "set_mesh_material", );
    if (phys_mesh_material_index(id, index, "set_mesh_material"))
        b3Shape_SetMeshMaterial(id, b3material(material), (int)index);
}

gs_phys_surface_material gs_phys_shape_mesh_material(gs_phys_shape s, int64_t index) {
    gs_phys_surface_material none;
    memset(&none, 0, sizeof none);
    PHYS_SHAPE(s, "mesh_material", none);
    if (!phys_mesh_material_index(id, index, "mesh_material")) return none;
    return gsmaterial(b3Shape_GetMeshSurfaceMaterial(id, (int)index));
}

gs_phys_filter gs_phys_shape_filter(gs_phys_shape s) {
    gs_phys_filter none = { 0, 0, 0 };
    PHYS_SHAPE(s, "filter", none);
    return gsfilter(b3Shape_GetFilter(id));
}

void gs_phys_shape_set_filter(gs_phys_shape s, gs_phys_filter filter, uint8_t invoke_contacts) {
    PHYS_SHAPE(s, "set_filter", );
    b3Shape_SetFilter(id, b3filter(filter), invoke_contacts);
}

void gs_phys_shape_enable_sensor_events(gs_phys_shape s, uint8_t flag) {
    PHYS_SHAPE(s, "enable_sensor_events", );
    b3Shape_EnableSensorEvents(id, flag);
}

uint8_t gs_phys_shape_sensor_events_enabled(gs_phys_shape s) {
    PHYS_SHAPE(s, "sensor_events_enabled", 0);
    return b3Shape_AreSensorEventsEnabled(id);
}

void gs_phys_shape_enable_contact_events(gs_phys_shape s, uint8_t flag) {
    PHYS_SHAPE(s, "enable_contact_events", );
    b3Shape_EnableContactEvents(id, flag);
}

uint8_t gs_phys_shape_contact_events_enabled(gs_phys_shape s) {
    PHYS_SHAPE(s, "contact_events_enabled", 0);
    return b3Shape_AreContactEventsEnabled(id);
}

void gs_phys_shape_enable_hit_events(gs_phys_shape s, uint8_t flag) {
    PHYS_SHAPE(s, "enable_hit_events", );
    b3Shape_EnableHitEvents(id, flag);
}

uint8_t gs_phys_shape_hit_events_enabled(gs_phys_shape s) {
    PHYS_SHAPE(s, "hit_events_enabled", 0);
    return b3Shape_AreHitEventsEnabled(id);
}

gs_phys_cast_output gs_phys_shape_ray_cast(gs_phys_shape s, gs_phys_float3 origin,
                                           gs_phys_float3 translation) {
    gs_phys_cast_output none;
    memset(&none, 0, sizeof none);
    PHYS_SHAPE(s, "ray_cast", none);
    return gscast(b3Shape_RayCast(id, b3v3(origin), b3v3(translation)));
}

/* The shape's geometry kind, or a misuse for another. */
static bool phys_shape_is(b3ShapeId id, b3ShapeType type, const char *fn, const char *what) {
    if (b3Shape_GetType(id) == type) return true;
    return phys_misuse("physics::%s: not a %s shape", fn, what);
}

gs_phys_sphere gs_phys_shape_sphere(gs_phys_shape s) {
    gs_phys_sphere none = { { 0, 0, 0 }, 0 };
    PHYS_SHAPE(s, "sphere", none);
    if (!phys_shape_is(id, b3_sphereShape, "sphere", "sphere")) return none;
    return gssphere(b3Shape_GetSphere(id));
}

gs_phys_capsule gs_phys_shape_capsule(gs_phys_shape s) {
    gs_phys_capsule none;
    memset(&none, 0, sizeof none);
    PHYS_SHAPE(s, "capsule", none);
    if (!phys_shape_is(id, b3_capsuleShape, "capsule", "capsule")) return none;
    return gscapsule(b3Shape_GetCapsule(id));
}

gs_phys_hull gs_phys_shape_hull(gs_phys_shape s) {
    gs_phys_hull none = { 0 };
    PHYS_SHAPE(s, "hull", none);
    if (!phys_shape_is(id, b3_hullShape, "hull", "hull")) return none;
    /* The world's copy lives only as long as its shapes: the program gets a
       copy of its own. */
    b3HullData *copy = b3CloneHull(b3Shape_GetHull(id));
    if (!copy) {
        phys_fail("cannot copy the hull");
        return none;
    }
    gs_phys_hull h = { phys_add(PHYS_HULL, copy) };
    if (!h.id) b3DestroyHull(copy);
    return h;
}

gs_phys_mesh gs_phys_shape_mesh(gs_phys_shape s) {
    gs_phys_mesh none = { 0 };
    PHYS_SHAPE(s, "mesh", none);
    if (!phys_shape_is(id, b3_meshShape, "mesh", "mesh")) return none;
    gs_phys_mesh m = { phys_find_data(PHYS_MESH, b3Shape_GetMesh(id).data) };
    return m;
}

gs_phys_float3 gs_phys_shape_mesh_scale(gs_phys_shape s) {
    PHYS_SHAPE(s, "mesh_scale", phys_zero3());
    if (!phys_shape_is(id, b3_meshShape, "mesh_scale", "mesh")) return phys_zero3();
    return gsf3(b3Shape_GetMesh(id).scale);
}

gs_phys_height_field gs_phys_shape_height_field(gs_phys_shape s) {
    gs_phys_height_field none = { 0 };
    PHYS_SHAPE(s, "height_field", none);
    if (!phys_shape_is(id, b3_heightShape, "height_field", "height field")) return none;
    gs_phys_height_field h = { phys_find_data(PHYS_HEIGHT_FIELD, b3Shape_GetHeightField(id)) };
    return h;
}

gs_phys_compound gs_phys_shape_compound(gs_phys_shape s) {
    gs_phys_compound none = { 0 };
    PHYS_SHAPE(s, "compound", none);
    if (!phys_shape_is(id, b3_compoundShape, "compound", "compound")) return none;
    gs_phys_compound c = { phys_find_user(PHYS_COMPOUND, id) };
    return c;
}

void gs_phys_shape_set_sphere(gs_phys_shape s, gs_phys_sphere sphere) {
    PHYS_SHAPE(s, "set_sphere", );
    if (!phys_radius_ok(sphere.radius, "set_sphere")) return;
    b3Sphere g = b3sphere(sphere);
    b3Shape_SetSphere(id, &g);
    phys_sweep();
}

void gs_phys_shape_set_capsule(gs_phys_shape s, gs_phys_capsule capsule) {
    PHYS_SHAPE(s, "set_capsule", );
    if (!phys_radius_ok(capsule.radius, "set_capsule")) return;
    b3Capsule g = b3capsule(capsule);
    b3Shape_SetCapsule(id, &g);
    phys_sweep();
}

void gs_phys_shape_set_hull(gs_phys_shape s, gs_phys_hull hull) {
    PHYS_SHAPE(s, "set_hull", );
    phys_item *h = phys_get(PHYS_HULL, hull.id, "set_hull");
    if (!h) return;
    b3Shape_SetHull(id, (const b3HullData *)h->data);
    phys_sweep();
}

void gs_phys_shape_set_mesh(gs_phys_shape s, gs_phys_mesh mesh, gs_phys_float3 scale) {
    PHYS_SHAPE(s, "set_mesh", );
    phys_item *m = phys_get(PHYS_MESH, mesh.id, "set_mesh");
    if (!m || !phys_scale_ok(scale, "set_mesh")) return;
    b3Shape_SetMesh(id, (const b3MeshData *)m->data, b3v3(scale));
    phys_add_user(m, id);
    phys_sweep();
}

void phys_put_manifolds(b3ContactData data, gs_phys_manifold_slice out, int64_t *n) {
    for (int i = 0; i < data.manifoldCount; i++, (*n)++) {
        if (*n >= out.len) continue;
        const b3Manifold *m = &data.manifolds[i];
        gs_phys_manifold *o = &out.data[*n];
        memset(o, 0, sizeof *o);
        o->contact = phys_contact_of(data.contactId);
        o->shape_a = phys_shape_of(data.shapeIdA);
        o->shape_b = phys_shape_of(data.shapeIdB);
        o->normal = gsf3(m->normal);
        o->twist_impulse = m->twistImpulse;
        o->friction_impulse = gsf3(m->frictionImpulse);
        o->rolling_impulse = gsf3(m->rollingImpulse);
        o->point_count = m->pointCount < 4 ? m->pointCount : 4;
        for (int k = 0; k < o->point_count; k++) {
            const b3ManifoldPoint *p = &m->points[k];
            gs_phys_manifold_point *q = &o->points[k];
            q->anchor_a = gsf3(p->anchorA);
            q->anchor_b = gsf3(p->anchorB);
            q->separation = p->separation;
            q->base_separation = p->baseSeparation;
            q->normal_impulse = p->normalImpulse;
            q->total_normal_impulse = p->totalNormalImpulse;
            q->normal_velocity = p->normalVelocity;
            q->feature_id = p->featureId;
            q->triangle_index = p->triangleIndex;
            q->persisted = p->persisted;
        }
    }
}

int64_t gs_phys_shape_contacts(gs_phys_shape s, gs_phys_manifold_slice out) {
    PHYS_SHAPE(s, "contacts", 0);
    int cap = b3Shape_GetContactCapacity(id);
    if (cap == 0) return 0;
    b3ContactData *data = (b3ContactData *)malloc((size_t)cap * sizeof(b3ContactData));
    int count = b3Shape_GetContactData(id, data, cap);
    int64_t n = 0;
    for (int i = 0; i < count; i++) phys_put_manifolds(data[i], out, &n);
    free(data);
    return n;
}

int64_t gs_phys_shape_sensor_overlaps(gs_phys_shape s, gs_phys_shape_slice out) {
    PHYS_SHAPE(s, "sensor_overlaps", 0);
    int cap = b3Shape_GetSensorCapacity(id);
    if (cap == 0) return 0;
    b3ShapeId *visitors = (b3ShapeId *)malloc((size_t)cap * sizeof(b3ShapeId));
    int count = b3Shape_GetSensorData(id, visitors, cap);
    int64_t n = 0;
    /* What a sensor overlapped may have been destroyed since the step. */
    for (int i = 0; i < count; i++) {
        if (!b3Shape_IsValid(visitors[i])) continue;
        if (n < out.len) out.data[n] = phys_shape_of(visitors[i]);
        n++;
    }
    free(visitors);
    return n;
}

gs_phys_aabb gs_phys_shape_aabb(gs_phys_shape s) {
    gs_phys_aabb none;
    memset(&none, 0, sizeof none);
    PHYS_SHAPE(s, "aabb", none);
    return gsbox(b3Shape_GetAABB(id));
}

gs_phys_mass_data gs_phys_shape_mass_data(gs_phys_shape s) {
    gs_phys_mass_data none;
    memset(&none, 0, sizeof none);
    PHYS_SHAPE(s, "mass_data", none);
    return gsmass(b3Shape_ComputeMassData(id));
}

gs_phys_float3 gs_phys_shape_closest_point(gs_phys_shape s, gs_phys_float3 target) {
    PHYS_SHAPE(s, "closest_point", phys_zero3());
    return gsf3(b3Shape_GetClosestPoint(id, b3v3(target)));
}

void gs_phys_shape_apply_wind(gs_phys_shape s, gs_phys_float3 wind, float drag, float lift,
                              float max_speed, uint8_t wake) {
    PHYS_SHAPE(s, "apply_wind", );
    b3Shape_ApplyWind(id, b3v3(wind), drag, lift, max_speed, wake);
}

/* --- contacts ------------------------------------------------------------- */

uint8_t gs_phys_contact_is_valid(gs_phys_contact c) {
    b3ContactId id = { c.index1, c.world0, c.reserved, c.generation };
    return c.index1 && b3Contact_IsValid(id);
}

int64_t gs_phys_contact_manifolds(gs_phys_contact c, gs_phys_manifold_slice out) {
    b3ContactId id;
    if (!phys_contact_id(c, "manifolds", &id)) return 0;
    int64_t n = 0;
    phys_put_manifolds(b3Contact_GetData(id), out, &n);
    return n;
}
