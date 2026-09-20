/* Geometry the layer owns -- hulls, meshes, height fields and baked
   compounds -- and the queries on geometry alone, outside any world: mass,
   bounds, ray and shape casts, overlaps, distance, time of impact and the
   character mover's plane solver. */

#include "physics_internal.h"

static gs_phys_aabb phys_no_box(void) {
    gs_phys_aabb r;
    memset(&r, 0, sizeof r);
    return r;
}

static gs_phys_cast_output phys_no_cast(void) {
    gs_phys_cast_output r;
    memset(&r, 0, sizeof r);
    return r;
}

static gs_phys_mass_data phys_no_mass(void) {
    gs_phys_mass_data r;
    memset(&r, 0, sizeof r);
    return r;
}

static bool phys_positive(float v) { return v > 0.0f && v <= FLT_MAX; }

/* The program's points as naturally aligned b3Vec3s, which Box3D reads and
   may reorder: the caller frees them. */
static b3Vec3 *phys_points(gs_phys_float3_slice points) {
    b3Vec3 *p = (b3Vec3 *)malloc((size_t)(points.len ? points.len : 1) * sizeof(b3Vec3));
    for (int64_t i = 0; i < points.len; i++) p[i] = b3v3(points.data[i]);
    return p;
}

/* A new handle for a Box3D allocation, or 0 with the reason. */
static uint32_t phys_own(phys_kind kind, void *data, const char *what) {
    if (!data) {
        phys_fail("%s", what);
        return 0;
    }
    uint32_t id = phys_add(kind, data);
    if (!id) {
        switch (kind) {
            case PHYS_HULL: b3DestroyHull((b3HullData *)data); break;
            case PHYS_MESH: b3DestroyMesh((b3MeshData *)data); break;
            case PHYS_HEIGHT_FIELD: b3DestroyHeightField((b3HeightFieldData *)data); break;
            case PHYS_COMPOUND: b3DestroyCompound((b3CompoundData *)data); break;
            default: break;
        }
    }
    return id;
}

/* --- hulls ---------------------------------------------------------------- */

gs_phys_hull gs_phys_create_hull(gs_phys_float3_slice points, int64_t max_vertices) {
    gs_phys_hull h = { 0 };
    phys_init();
    if (points.len > 100000) {
        phys_misuse("physics::create_hull: %lld points", (long long)points.len);
        return h;
    }
    int limit = max_vertices < 4 ? 4 : max_vertices > B3_MAX_HULL_VERTICES ? B3_MAX_HULL_VERTICES
                                                                           : (int)max_vertices;
    b3Vec3 *p = phys_points(points);
    b3HullData *hull = b3CreateHull(p, (int)points.len, limit);
    free(p);
    h.id = phys_own(PHYS_HULL, hull, "the points make no hull: fewer than four, or all in a plane");
    return h;
}

gs_phys_hull gs_phys_create_box_hull(gs_phys_float3 half_extents, gs_phys_transform frame) {
    gs_phys_hull h = { 0 };
    phys_init();
    if (!phys_positive(half_extents.x) || !phys_positive(half_extents.y) ||
        !phys_positive(half_extents.z)) {
        phys_misuse("physics::create_box_hull: half extents of %g, %g, %g", (double)half_extents.x,
                    (double)half_extents.y, (double)half_extents.z);
        return h;
    }
    b3BoxHull box = b3MakeTransformedBoxHull(half_extents.x, half_extents.y, half_extents.z,
                                             b3xf(frame));
    h.id = phys_own(PHYS_HULL, b3CloneHull(&box.base), "cannot copy the box hull");
    return h;
}

gs_phys_hull gs_phys_create_cylinder_hull(float height, float radius, float y_offset,
                                          int64_t sides) {
    gs_phys_hull h = { 0 };
    phys_init();
    if (!phys_positive(height) || !phys_positive(radius) || sides < 3 || sides > 32) {
        phys_misuse("physics::create_cylinder_hull: height %g, radius %g, %lld sides (3 to 32)",
                    (double)height, (double)radius, (long long)sides);
        return h;
    }
    h.id = phys_own(PHYS_HULL, b3CreateCylinder(height, radius, y_offset, (int)sides),
                    "cannot make the cylinder");
    return h;
}

gs_phys_hull gs_phys_create_cone_hull(float height, float radius1, float radius2, int64_t slices) {
    gs_phys_hull h = { 0 };
    phys_init();
    if (!phys_positive(height) || !phys_positive(radius1) || !phys_positive(radius2) ||
        slices < 4 || slices > 32) {
        phys_misuse("physics::create_cone_hull: height %g, radii %g and %g, %lld slices (4 to 32)",
                    (double)height, (double)radius1, (double)radius2, (long long)slices);
        return h;
    }
    h.id = phys_own(PHYS_HULL, b3CreateCone(height, radius1, radius2, (int)slices),
                    "cannot make the cone");
    return h;
}

gs_phys_hull gs_phys_create_rock_hull(float radius) {
    gs_phys_hull h = { 0 };
    phys_init();
    if (!phys_positive(radius)) {
        phys_misuse("physics::create_rock_hull: a radius of %g", (double)radius);
        return h;
    }
    h.id = phys_own(PHYS_HULL, b3CreateRock(radius), "cannot make the rock");
    return h;
}

gs_phys_hull gs_phys_hull_transformed(gs_phys_hull h, gs_phys_transform transform,
                                      gs_phys_float3 scale) {
    gs_phys_hull r = { 0 };
    phys_item *item = phys_get(PHYS_HULL, h.id, "transformed");
    if (!item) return r;
    if (!b3IsValidFloat(scale.x) || !b3IsValidFloat(scale.y) || !b3IsValidFloat(scale.z) ||
        scale.x == 0.0f || scale.y == 0.0f || scale.z == 0.0f) {
        phys_misuse("physics::transformed: a scale of %g, %g, %g", (double)scale.x, (double)scale.y,
                    (double)scale.z);
        return r;
    }
    r.id = phys_own(
        PHYS_HULL,
        b3CloneAndTransformHull((const b3HullData *)item->data, b3xf(transform), b3v3(scale)),
        "the transformed hull is degenerate");
    return r;
}

void gs_phys_hull_destroy(gs_phys_hull h) { phys_release(PHYS_HULL, h.id, "destroy"); }
uint8_t gs_phys_hull_is_valid(gs_phys_hull h) { return phys_is_live(PHYS_HULL, h.id); }

gs_phys_hull_info gs_phys_hull_info_of(gs_phys_hull h) {
    gs_phys_hull_info r;
    memset(&r, 0, sizeof r);
    phys_item *item = phys_get(PHYS_HULL, h.id, "info");
    if (!item) return r;
    const b3HullData *d = (const b3HullData *)item->data;
    r.vertex_count = d->vertexCount;
    r.half_edge_count = d->edgeCount;
    r.face_count = d->faceCount;
    r.byte_count = d->byteCount;
    r.volume = d->volume;
    r.surface_area = d->surfaceArea;
    r.inner_radius = d->innerRadius;
    r.center = gsf3(d->center);
    r.central_inertia = gsm3(d->centralInertia);
    r.aabb = gsbox(d->aabb);
    return r;
}

int64_t gs_phys_hull_vertices(gs_phys_hull h, gs_phys_float3_slice out) {
    phys_item *item = phys_get(PHYS_HULL, h.id, "vertices");
    if (!item) return 0;
    const b3HullData *d = (const b3HullData *)item->data;
    const b3Vec3 *points = b3GetHullPoints(d);
    for (int64_t i = 0; i < d->vertexCount && i < out.len; i++) out.data[i] = gsf3(points[i]);
    return d->vertexCount;
}

int64_t gs_phys_hull_triangles(gs_phys_hull h, gs_phys_int3_slice out) {
    phys_item *item = phys_get(PHYS_HULL, h.id, "triangles");
    if (!item) return 0;
    const b3HullData *d = (const b3HullData *)item->data;
    const b3HullHalfEdge *edges = b3GetHullEdges(d);
    const b3HullFace *faces = b3GetHullFaces(d);
    int64_t n = 0;
    /* Each face is a convex polygon of half-edges: a fan from its first
       vertex, in the face's own winding. */
    for (int f = 0; f < d->faceCount; f++) {
        int first = faces[f].edge;
        int v0 = edges[first].origin;
        int e = edges[first].next;
        int v1 = edges[e].origin;
        for (e = edges[e].next; e != first; e = edges[e].next, n++) {
            int v2 = edges[e].origin;
            if (n < out.len) {
                gs_phys_int3 t = { v0, v1, v2 };
                out.data[n] = t;
            }
            v1 = v2;
        }
    }
    return n;
}

/* --- meshes --------------------------------------------------------------- */

gs_phys_mesh gs_phys_create_mesh(gs_phys_float3_slice vertices, gs_phys_i32_slice indices,
                                 gs_phys_bytes material_indices, gs_phys_mesh_def def) {
    gs_phys_mesh m = { 0 };
    phys_init();
    int64_t triangles = indices.len / 3;
    if (vertices.len < 3 || vertices.len > INT32_MAX || indices.len % 3 != 0 || triangles < 1 ||
        indices.len > INT32_MAX) {
        phys_misuse("physics::create_mesh: %lld vertices and %lld indices (three per triangle)",
                    (long long)vertices.len, (long long)indices.len);
        return m;
    }
    for (int64_t i = 0; i < indices.len; i++) {
        if (indices.data[i] < 0 || indices.data[i] >= vertices.len) {
            phys_misuse("physics::create_mesh: index %lld is %d, of %lld vertices", (long long)i,
                        indices.data[i], (long long)vertices.len);
            return m;
        }
    }
    if (material_indices.len != 0 && material_indices.len != triangles) {
        phys_misuse("physics::create_mesh: %lld material indices for %lld triangles",
                    (long long)material_indices.len, (long long)triangles);
        return m;
    }
    /* Box3D's definition takes writable arrays: copies of the program's. */
    b3Vec3 *v = phys_points(vertices);
    int32_t *idx = (int32_t *)malloc((size_t)indices.len * sizeof(int32_t));
    memcpy(idx, indices.data, (size_t)indices.len * sizeof(int32_t));
    uint8_t *mats = NULL;
    if (material_indices.len) {
        mats = (uint8_t *)malloc((size_t)material_indices.len);
        memcpy(mats, material_indices.data, (size_t)material_indices.len);
    }
    b3MeshDef d;
    memset(&d, 0, sizeof d);
    d.vertices = v;
    d.stride = sizeof(b3Vec3);
    d.indices = idx;
    d.materialIndices = mats;
    d.weldTolerance = def.weld_tolerance;
    d.vertexCount = (int)vertices.len;
    d.triangleCount = (int)triangles;
    d.weldVertices = def.weld_vertices;
    d.useMedianSplit = def.use_median_split;
    d.identifyEdges = def.identify_edges;
    d.clockWiseWinding = def.clockwise;
    b3MeshData *mesh = b3CreateMesh(&d, NULL, 0);
    free(v);
    free(idx);
    free(mats);
    m.id = phys_own(PHYS_MESH, mesh, "cannot make the mesh");
    return m;
}

gs_phys_mesh gs_phys_create_grid_mesh(int64_t x_count, int64_t z_count, float cell_width,
                                      int64_t material_count, uint8_t identify_edges) {
    gs_phys_mesh m = { 0 };
    phys_init();
    if (x_count < 1 || z_count < 1 || x_count * z_count > (1 << 24) || !phys_positive(cell_width) ||
        material_count < 0 || material_count > 255) {
        phys_misuse("physics::create_grid_mesh: %lld x %lld cells of %g, %lld materials",
                    (long long)x_count, (long long)z_count, (double)cell_width,
                    (long long)material_count);
        return m;
    }
    m.id = phys_own(PHYS_MESH,
                    b3CreateGridMesh((int)x_count, (int)z_count, cell_width, (int)material_count,
                                     identify_edges),
                    "cannot make the grid mesh");
    return m;
}

gs_phys_mesh gs_phys_create_wave_mesh(int64_t x_count, int64_t z_count, float cell_width,
                                      float amplitude, float row_frequency,
                                      float column_frequency) {
    gs_phys_mesh m = { 0 };
    phys_init();
    if (x_count < 1 || z_count < 1 || x_count * z_count > (1 << 24) || !phys_positive(cell_width)) {
        phys_misuse("physics::create_wave_mesh: %lld x %lld cells of %g", (long long)x_count,
                    (long long)z_count, (double)cell_width);
        return m;
    }
    m.id = phys_own(PHYS_MESH,
                    b3CreateWaveMesh((int)x_count, (int)z_count, cell_width, amplitude,
                                     row_frequency, column_frequency),
                    "cannot make the wave mesh");
    return m;
}

gs_phys_mesh gs_phys_create_torus_mesh(int64_t radial_resolution, int64_t tubular_resolution,
                                       float radius, float thickness) {
    gs_phys_mesh m = { 0 };
    phys_init();
    if (radial_resolution < 3 || tubular_resolution < 3 ||
        radial_resolution * tubular_resolution > (1 << 24) || !phys_positive(radius) ||
        !phys_positive(thickness)) {
        phys_misuse("physics::create_torus_mesh: %lld x %lld segments, radius %g, thickness %g",
                    (long long)radial_resolution, (long long)tubular_resolution, (double)radius,
                    (double)thickness);
        return m;
    }
    m.id = phys_own(PHYS_MESH,
                    b3CreateTorusMesh((int)radial_resolution, (int)tubular_resolution, radius,
                                      thickness),
                    "cannot make the torus mesh");
    return m;
}

static bool phys_extent_ok(gs_phys_float3 extent, const char *fn) {
    if (phys_positive(extent.x) && phys_positive(extent.y) && phys_positive(extent.z)) return true;
    return phys_misuse("physics::%s: an extent of %g, %g, %g", fn, (double)extent.x,
                       (double)extent.y, (double)extent.z);
}

gs_phys_mesh gs_phys_create_box_mesh(gs_phys_float3 center, gs_phys_float3 extent,
                                     uint8_t identify_edges) {
    gs_phys_mesh m = { 0 };
    phys_init();
    if (!phys_extent_ok(extent, "create_box_mesh")) return m;
    m.id = phys_own(PHYS_MESH, b3CreateBoxMesh(b3v3(center), b3v3(extent), identify_edges),
                    "cannot make the box mesh");
    return m;
}

gs_phys_mesh gs_phys_create_hollow_box_mesh(gs_phys_float3 center, gs_phys_float3 extent) {
    gs_phys_mesh m = { 0 };
    phys_init();
    if (!phys_extent_ok(extent, "create_hollow_box_mesh")) return m;
    m.id = phys_own(PHYS_MESH, b3CreateHollowBoxMesh(b3v3(center), b3v3(extent)),
                    "cannot make the hollow box mesh");
    return m;
}

gs_phys_mesh gs_phys_create_platform_mesh(gs_phys_float3 center, float height, float top_width,
                                          float bottom_width) {
    gs_phys_mesh m = { 0 };
    phys_init();
    if (!phys_positive(height) || !phys_positive(top_width) || !phys_positive(bottom_width)) {
        phys_misuse("physics::create_platform_mesh: height %g, widths %g and %g", (double)height,
                    (double)top_width, (double)bottom_width);
        return m;
    }
    m.id = phys_own(PHYS_MESH, b3CreatePlatformMesh(b3v3(center), height, top_width, bottom_width),
                    "cannot make the platform mesh");
    return m;
}

void gs_phys_mesh_destroy(gs_phys_mesh m) { phys_release(PHYS_MESH, m.id, "destroy"); }
uint8_t gs_phys_mesh_is_valid(gs_phys_mesh m) { return phys_is_live(PHYS_MESH, m.id); }

gs_phys_mesh_info gs_phys_mesh_info_of(gs_phys_mesh m) {
    gs_phys_mesh_info r;
    memset(&r, 0, sizeof r);
    phys_item *item = phys_get(PHYS_MESH, m.id, "info");
    if (!item) return r;
    const b3MeshData *d = (const b3MeshData *)item->data;
    r.vertex_count = d->vertexCount;
    r.triangle_count = d->triangleCount;
    r.material_count = d->materialCount;
    r.degenerate_count = d->degenerateCount;
    r.node_count = d->nodeCount;
    r.tree_height = d->treeHeight;
    r.byte_count = d->byteCount;
    r.surface_area = d->surfaceArea;
    r.bounds = gsbox(d->bounds);
    return r;
}

int64_t gs_phys_mesh_vertices(gs_phys_mesh m, gs_phys_float3_slice out) {
    phys_item *item = phys_get(PHYS_MESH, m.id, "vertices");
    if (!item) return 0;
    const b3MeshData *d = (const b3MeshData *)item->data;
    const b3Vec3 *v = b3GetMeshVertices(d);
    for (int64_t i = 0; i < d->vertexCount && i < out.len; i++) out.data[i] = gsf3(v[i]);
    return d->vertexCount;
}

int64_t gs_phys_mesh_triangles(gs_phys_mesh m, gs_phys_int3_slice out) {
    phys_item *item = phys_get(PHYS_MESH, m.id, "triangles");
    if (!item) return 0;
    const b3MeshData *d = (const b3MeshData *)item->data;
    const b3MeshTriangle *t = b3GetMeshTriangles(d);
    for (int64_t i = 0; i < d->triangleCount && i < out.len; i++) {
        gs_phys_int3 r = { t[i].index1, t[i].index2, t[i].index3 };
        out.data[i] = r;
    }
    return d->triangleCount;
}

/* --- height fields -------------------------------------------------------- */

gs_phys_height_field gs_phys_create_height_field(gs_phys_f32_slice heights,
                                                 gs_phys_bytes material_indices,
                                                 gs_phys_height_field_def def) {
    gs_phys_height_field h = { 0 };
    phys_init();
    int64_t cells = (int64_t)(def.count_x - 1) * (def.count_z - 1);
    if (def.count_x < 2 || def.count_z < 2 || (int64_t)def.count_x * def.count_z != heights.len ||
        heights.len > (1 << 26)) {
        phys_misuse("physics::create_height_field: %lld heights for %d x %d",
                    (long long)heights.len, def.count_x, def.count_z);
        return h;
    }
    if (material_indices.len != 0 && material_indices.len != cells) {
        phys_misuse("physics::create_height_field: %lld material indices for %lld cells",
                    (long long)material_indices.len, (long long)cells);
        return h;
    }
    if (!phys_positive(def.scale.x) || !phys_positive(def.scale.y) || !phys_positive(def.scale.z)) {
        phys_misuse("physics::create_height_field: a scale of %g, %g, %g", (double)def.scale.x,
                    (double)def.scale.y, (double)def.scale.z);
        return h;
    }
    float lo = def.min_height, hi = def.max_height;
    if (lo == 0.0f && hi == 0.0f) {
        /* No range given: the heights' own. */
        lo = FLT_MAX;
        hi = -FLT_MAX;
        for (int64_t i = 0; i < heights.len; i++) {
            if (heights.data[i] < lo) lo = heights.data[i];
            if (heights.data[i] > hi) hi = heights.data[i];
        }
    }
    if (!(lo <= hi) || !b3IsValidFloat(lo) || !b3IsValidFloat(hi)) {
        phys_misuse("physics::create_height_field: heights from %g to %g", (double)lo, (double)hi);
        return h;
    }
    float *hs = (float *)malloc((size_t)heights.len * sizeof(float));
    memcpy(hs, heights.data, (size_t)heights.len * sizeof(float));
    uint8_t *mats = NULL;
    if (material_indices.len) {
        mats = (uint8_t *)malloc((size_t)material_indices.len);
        memcpy(mats, material_indices.data, (size_t)material_indices.len);
    }
    b3HeightFieldDef d;
    memset(&d, 0, sizeof d);
    d.heights = hs;
    d.materialIndices = mats;
    d.scale = b3v3(def.scale);
    d.countX = def.count_x;
    d.countZ = def.count_z;
    d.globalMinimumHeight = lo;
    d.globalMaximumHeight = hi;
    d.clockwiseWinding = def.clockwise;
    b3HeightFieldData *hf = b3CreateHeightField(&d);
    free(hs);
    free(mats);
    h.id = phys_own(PHYS_HEIGHT_FIELD, hf, "cannot make the height field");
    return h;
}

static bool phys_grid_ok(int64_t rows, int64_t columns, gs_phys_float3 scale, const char *fn) {
    if (rows >= 2 && columns >= 2 && rows * columns <= (1 << 24) && phys_positive(scale.x) &&
        phys_positive(scale.y) && phys_positive(scale.z))
        return true;
    return phys_misuse("physics::%s: %lld x %lld heights at a scale of %g, %g, %g", fn,
                       (long long)rows, (long long)columns, (double)scale.x, (double)scale.y,
                       (double)scale.z);
}

gs_phys_height_field gs_phys_create_grid_height_field(int64_t rows, int64_t columns,
                                                      gs_phys_float3 scale, uint8_t make_holes) {
    gs_phys_height_field h = { 0 };
    phys_init();
    if (!phys_grid_ok(rows, columns, scale, "create_grid_height_field")) return h;
    h.id =
        phys_own(PHYS_HEIGHT_FIELD, b3CreateGrid((int)rows, (int)columns, b3v3(scale), make_holes),
                 "cannot make the grid height field");
    return h;
}

gs_phys_height_field gs_phys_create_wave_height_field(int64_t rows, int64_t columns,
                                                      gs_phys_float3 scale, float row_frequency,
                                                      float column_frequency, uint8_t make_holes) {
    gs_phys_height_field h = { 0 };
    phys_init();
    if (!phys_grid_ok(rows, columns, scale, "create_wave_height_field")) return h;
    h.id = phys_own(PHYS_HEIGHT_FIELD,
                    b3CreateWave((int)rows, (int)columns, b3v3(scale), row_frequency,
                                 column_frequency, make_holes),
                    "cannot make the wave height field");
    return h;
}

void gs_phys_height_field_destroy(gs_phys_height_field h) {
    phys_release(PHYS_HEIGHT_FIELD, h.id, "destroy");
}

uint8_t gs_phys_height_field_is_valid(gs_phys_height_field h) {
    return phys_is_live(PHYS_HEIGHT_FIELD, h.id);
}

gs_phys_height_field_info gs_phys_height_field_info_of(gs_phys_height_field h) {
    gs_phys_height_field_info r;
    memset(&r, 0, sizeof r);
    phys_item *item = phys_get(PHYS_HEIGHT_FIELD, h.id, "info");
    if (!item) return r;
    const b3HeightFieldData *d = (const b3HeightFieldData *)item->data;
    r.columns = d->columnCount;
    r.rows = d->rowCount;
    r.byte_count = d->byteCount;
    r.min_height = d->minHeight;
    r.max_height = d->maxHeight;
    r.scale = gsf3(d->scale);
    r.aabb = gsbox(d->aabb);
    return r;
}

/* --- baked compounds ------------------------------------------------------ */

gs_phys_compound gs_phys_create_compound(gs_phys_compound_sphere_slice spheres,
                                         gs_phys_compound_capsule_slice capsules,
                                         gs_phys_compound_hull_slice hulls,
                                         gs_phys_compound_mesh_slice meshes) {
    gs_phys_compound c = { 0 };
    phys_init();
    int64_t total = spheres.len + capsules.len + hulls.len + meshes.len;
    if (total < 1 || total > (1 << 20)) {
        phys_misuse("physics::create_compound: %lld children", (long long)total);
        return c;
    }
    b3CompoundDef d;
    memset(&d, 0, sizeof d);
    b3CompoundSphereDef *sd = (b3CompoundSphereDef *)calloc((size_t)spheres.len + 1, sizeof *sd);
    b3CompoundCapsuleDef *cd = (b3CompoundCapsuleDef *)calloc((size_t)capsules.len + 1, sizeof *cd);
    b3CompoundHullDef *hd = (b3CompoundHullDef *)calloc((size_t)hulls.len + 1, sizeof *hd);
    b3CompoundMeshDef *md = (b3CompoundMeshDef *)calloc((size_t)meshes.len + 1, sizeof *md);
    b3SurfaceMaterial *mm = (b3SurfaceMaterial *)calloc(
        (size_t)(meshes.len + 1) * B3_MAX_COMPOUND_MESH_MATERIALS, sizeof *mm);
    bool ok = true;
    for (int64_t i = 0; i < spheres.len; i++) {
        sd[i].sphere = b3sphere(spheres.data[i].sphere);
        sd[i].material = b3material(spheres.data[i].material);
    }
    for (int64_t i = 0; i < capsules.len; i++) {
        cd[i].capsule = b3capsule(capsules.data[i].capsule);
        cd[i].material = b3material(capsules.data[i].material);
    }
    for (int64_t i = 0; i < hulls.len && ok; i++) {
        phys_item *item = phys_get(PHYS_HULL, hulls.data[i].hull.id, "create_compound");
        ok = item != NULL;
        if (!ok) break;
        hd[i].hull = (const b3HullData *)item->data;
        hd[i].transform = b3xf(hulls.data[i].transform);
        hd[i].material = b3material(hulls.data[i].material);
    }
    for (int64_t i = 0; i < meshes.len && ok; i++) {
        phys_item *item = phys_get(PHYS_MESH, meshes.data[i].mesh.id, "create_compound");
        ok = item != NULL;
        if (!ok) break;
        const b3MeshData *mesh = (const b3MeshData *)item->data;
        /* Box3D wants one material per material the mesh has. */
        if (mesh->materialCount > B3_MAX_COMPOUND_MESH_MATERIALS) {
            ok = phys_misuse("physics::create_compound: a mesh of %d materials (at most %d)",
                             mesh->materialCount, B3_MAX_COMPOUND_MESH_MATERIALS);
            break;
        }
        b3SurfaceMaterial *materials = mm + i * B3_MAX_COMPOUND_MESH_MATERIALS;
        for (int k = 0; k < mesh->materialCount; k++)
            materials[k] = b3material(meshes.data[i].material);
        md[i].meshData = mesh;
        md[i].transform = b3xf(meshes.data[i].transform);
        md[i].scale = b3v3(meshes.data[i].scale);
        md[i].materials = materials;
        md[i].materialCount = mesh->materialCount;
    }
    if (ok) {
        d.spheres = sd;
        d.sphereCount = (int)spheres.len;
        d.capsules = cd;
        d.capsuleCount = (int)capsules.len;
        d.hulls = hd;
        d.hullCount = (int)hulls.len;
        d.meshes = md;
        d.meshCount = (int)meshes.len;
        c.id = phys_own(PHYS_COMPOUND, b3CreateCompound(&d), "cannot make the compound");
    }
    free(sd);
    free(cd);
    free(hd);
    free(md);
    free(mm);
    return c;
}

void gs_phys_compound_destroy(gs_phys_compound c) { phys_release(PHYS_COMPOUND, c.id, "destroy"); }
uint8_t gs_phys_compound_is_valid(gs_phys_compound c) { return phys_is_live(PHYS_COMPOUND, c.id); }

gs_phys_compound_info gs_phys_compound_info_of(gs_phys_compound c) {
    gs_phys_compound_info r;
    memset(&r, 0, sizeof r);
    phys_item *item = phys_get(PHYS_COMPOUND, c.id, "info");
    if (!item) return r;
    const b3CompoundData *d = (const b3CompoundData *)item->data;
    r.sphere_count = d->sphereCount;
    r.capsule_count = d->capsuleCount;
    r.hull_count = d->hullCount;
    r.mesh_count = d->meshCount;
    r.material_count = d->materialCount;
    r.byte_count = d->byteCount;
    return r;
}

/* --- geometry on its own -------------------------------------------------- */

#define PHYS_GEOMETRY(kind, handle, fn, type, fail) \
    phys_item *item = phys_get(kind, (handle).id, fn); \
    if (!item) return fail; \
    const type *g = (const type *)item->data

gs_phys_mass_data gs_phys_sphere_mass(gs_phys_sphere s, float density) {
    b3Sphere g = b3sphere(s);
    return gsmass(b3ComputeSphereMass(&g, density));
}

gs_phys_mass_data gs_phys_capsule_mass(gs_phys_capsule c, float density) {
    b3Capsule g = b3capsule(c);
    return gsmass(b3ComputeCapsuleMass(&g, density));
}

gs_phys_mass_data gs_phys_hull_mass(gs_phys_hull h, float density) {
    PHYS_GEOMETRY(PHYS_HULL, h, "compute_mass", b3HullData, phys_no_mass());
    return gsmass(b3ComputeHullMass(g, density));
}

gs_phys_aabb gs_phys_sphere_aabb(gs_phys_sphere s, gs_phys_transform transform) {
    b3Sphere g = b3sphere(s);
    return gsbox(b3ComputeSphereAABB(&g, b3xf(transform)));
}

gs_phys_aabb gs_phys_capsule_aabb(gs_phys_capsule c, gs_phys_transform transform) {
    b3Capsule g = b3capsule(c);
    return gsbox(b3ComputeCapsuleAABB(&g, b3xf(transform)));
}

gs_phys_aabb gs_phys_hull_aabb(gs_phys_hull h, gs_phys_transform transform) {
    PHYS_GEOMETRY(PHYS_HULL, h, "compute_aabb", b3HullData, phys_no_box());
    return gsbox(b3ComputeHullAABB(g, b3xf(transform)));
}

gs_phys_aabb gs_phys_mesh_aabb(gs_phys_mesh m, gs_phys_transform transform, gs_phys_float3 scale) {
    PHYS_GEOMETRY(PHYS_MESH, m, "compute_aabb", b3MeshData, phys_no_box());
    return gsbox(b3ComputeMeshAABB(g, b3xf(transform), b3v3(scale)));
}

gs_phys_aabb gs_phys_height_field_aabb(gs_phys_height_field h, gs_phys_transform transform) {
    PHYS_GEOMETRY(PHYS_HEIGHT_FIELD, h, "compute_aabb", b3HeightFieldData, phys_no_box());
    return gsbox(b3ComputeHeightFieldAABB(g, b3xf(transform)));
}

gs_phys_aabb gs_phys_compound_aabb(gs_phys_compound c, gs_phys_transform transform) {
    PHYS_GEOMETRY(PHYS_COMPOUND, c, "compute_aabb", b3CompoundData, phys_no_box());
    return gsbox(b3ComputeCompoundAABB(g, b3xf(transform)));
}

/* A ray in the geometry's own frame, or a misuse for one Box3D rejects. */
static bool phys_ray(gs_phys_float3 origin, gs_phys_float3 translation, float max_fraction,
                     b3RayCastInput *ray) {
    phys_init();
    ray->origin = b3v3(origin);
    ray->translation = b3v3(translation);
    ray->maxFraction = max_fraction;
    if (b3IsValidRay(ray)) return true;
    return phys_misuse("physics::ray_cast: a ray from %g, %g, %g by %g, %g, %g to fraction %g",
                       (double)origin.x, (double)origin.y, (double)origin.z, (double)translation.x,
                       (double)translation.y, (double)translation.z, (double)max_fraction);
}

gs_phys_cast_output gs_phys_sphere_ray_cast(gs_phys_sphere s, gs_phys_float3 origin,
                                            gs_phys_float3 translation, float max_fraction) {
    b3RayCastInput ray;
    if (!phys_ray(origin, translation, max_fraction, &ray)) return phys_no_cast();
    b3Sphere g = b3sphere(s);
    return gscast(b3RayCastSphere(&g, &ray));
}

gs_phys_cast_output gs_phys_capsule_ray_cast(gs_phys_capsule c, gs_phys_float3 origin,
                                             gs_phys_float3 translation, float max_fraction) {
    b3RayCastInput ray;
    if (!phys_ray(origin, translation, max_fraction, &ray)) return phys_no_cast();
    b3Capsule g = b3capsule(c);
    return gscast(b3RayCastCapsule(&g, &ray));
}

gs_phys_cast_output gs_phys_hull_ray_cast(gs_phys_hull h, gs_phys_float3 origin,
                                          gs_phys_float3 translation, float max_fraction) {
    PHYS_GEOMETRY(PHYS_HULL, h, "ray_cast", b3HullData, phys_no_cast());
    b3RayCastInput ray;
    if (!phys_ray(origin, translation, max_fraction, &ray)) return phys_no_cast();
    return gscast(b3RayCastHull(g, &ray));
}

gs_phys_cast_output gs_phys_mesh_ray_cast(gs_phys_mesh m, gs_phys_float3 scale,
                                          gs_phys_float3 origin, gs_phys_float3 translation,
                                          float max_fraction) {
    PHYS_GEOMETRY(PHYS_MESH, m, "ray_cast", b3MeshData, phys_no_cast());
    b3RayCastInput ray;
    if (!phys_ray(origin, translation, max_fraction, &ray)) return phys_no_cast();
    b3Mesh mesh = { g, b3v3(scale) };
    return gscast(b3RayCastMesh(&mesh, &ray));
}

gs_phys_cast_output gs_phys_height_field_ray_cast(gs_phys_height_field h, gs_phys_float3 origin,
                                                  gs_phys_float3 translation, float max_fraction) {
    PHYS_GEOMETRY(PHYS_HEIGHT_FIELD, h, "ray_cast", b3HeightFieldData, phys_no_cast());
    b3RayCastInput ray;
    if (!phys_ray(origin, translation, max_fraction, &ray)) return phys_no_cast();
    return gscast(b3RayCastHeightField(g, &ray));
}

gs_phys_cast_output gs_phys_compound_ray_cast(gs_phys_compound c, gs_phys_float3 origin,
                                              gs_phys_float3 translation, float max_fraction) {
    PHYS_GEOMETRY(PHYS_COMPOUND, c, "ray_cast", b3CompoundData, phys_no_cast());
    b3RayCastInput ray;
    if (!phys_ray(origin, translation, max_fraction, &ray)) return phys_no_cast();
    return gscast(b3RayCastCompound(g, &ray));
}

uint8_t gs_phys_sphere_overlap(gs_phys_sphere s, gs_phys_transform transform,
                               gs_phys_float3_slice points, float radius) {
    phys_proxy p;
    if (!phys_make_proxy(&p, points, radius, "overlap")) return 0;
    b3Sphere g = b3sphere(s);
    return b3OverlapSphere(&g, b3xf(transform), &p.proxy);
}

uint8_t gs_phys_capsule_overlap(gs_phys_capsule c, gs_phys_transform transform,
                                gs_phys_float3_slice points, float radius) {
    phys_proxy p;
    if (!phys_make_proxy(&p, points, radius, "overlap")) return 0;
    b3Capsule g = b3capsule(c);
    return b3OverlapCapsule(&g, b3xf(transform), &p.proxy);
}

uint8_t gs_phys_hull_overlap(gs_phys_hull h, gs_phys_transform transform,
                             gs_phys_float3_slice points, float radius) {
    PHYS_GEOMETRY(PHYS_HULL, h, "overlap", b3HullData, 0);
    phys_proxy p;
    if (!phys_make_proxy(&p, points, radius, "overlap")) return 0;
    return b3OverlapHull(g, b3xf(transform), &p.proxy);
}

uint8_t gs_phys_mesh_overlap(gs_phys_mesh m, gs_phys_float3 scale, gs_phys_transform transform,
                             gs_phys_float3_slice points, float radius) {
    PHYS_GEOMETRY(PHYS_MESH, m, "overlap", b3MeshData, 0);
    phys_proxy p;
    if (!phys_make_proxy(&p, points, radius, "overlap")) return 0;
    b3Mesh mesh = { g, b3v3(scale) };
    return b3OverlapMesh(&mesh, b3xf(transform), &p.proxy);
}

uint8_t gs_phys_height_field_overlap(gs_phys_height_field h, gs_phys_transform transform,
                                     gs_phys_float3_slice points, float radius) {
    PHYS_GEOMETRY(PHYS_HEIGHT_FIELD, h, "overlap", b3HeightFieldData, 0);
    phys_proxy p;
    if (!phys_make_proxy(&p, points, radius, "overlap")) return 0;
    return b3OverlapHeightField(g, b3xf(transform), &p.proxy);
}

uint8_t gs_phys_compound_overlap(gs_phys_compound c, gs_phys_transform transform,
                                 gs_phys_float3_slice points, float radius) {
    PHYS_GEOMETRY(PHYS_COMPOUND, c, "overlap", b3CompoundData, 0);
    phys_proxy p;
    if (!phys_make_proxy(&p, points, radius, "overlap")) return 0;
    return b3OverlapCompound(g, b3xf(transform), &p.proxy);
}

/* A shape cast's input: the moving points, in the geometry's frame. */
static bool phys_shape_cast_input(b3ShapeCastInput *in, phys_proxy *p, gs_phys_float3_slice points,
                                  float radius, gs_phys_float3 translation, float max_fraction,
                                  uint8_t can_encroach) {
    if (!phys_make_proxy(p, points, radius, "shape_cast")) return false;
    in->proxy = p->proxy;
    in->translation = b3v3(translation);
    in->maxFraction = max_fraction;
    in->canEncroach = can_encroach;
    return true;
}

gs_phys_cast_output gs_phys_sphere_shape_cast(gs_phys_sphere s, gs_phys_float3_slice points,
                                              float radius, gs_phys_float3 translation,
                                              float max_fraction, uint8_t can_encroach) {
    phys_proxy p;
    b3ShapeCastInput in;
    if (!phys_shape_cast_input(&in, &p, points, radius, translation, max_fraction, can_encroach))
        return phys_no_cast();
    b3Sphere g = b3sphere(s);
    return gscast(b3ShapeCastSphere(&g, &in));
}

gs_phys_cast_output gs_phys_capsule_shape_cast(gs_phys_capsule c, gs_phys_float3_slice points,
                                               float radius, gs_phys_float3 translation,
                                               float max_fraction, uint8_t can_encroach) {
    phys_proxy p;
    b3ShapeCastInput in;
    if (!phys_shape_cast_input(&in, &p, points, radius, translation, max_fraction, can_encroach))
        return phys_no_cast();
    b3Capsule g = b3capsule(c);
    return gscast(b3ShapeCastCapsule(&g, &in));
}

gs_phys_cast_output gs_phys_hull_shape_cast(gs_phys_hull h, gs_phys_float3_slice points,
                                            float radius, gs_phys_float3 translation,
                                            float max_fraction, uint8_t can_encroach) {
    PHYS_GEOMETRY(PHYS_HULL, h, "shape_cast", b3HullData, phys_no_cast());
    phys_proxy p;
    b3ShapeCastInput in;
    if (!phys_shape_cast_input(&in, &p, points, radius, translation, max_fraction, can_encroach))
        return phys_no_cast();
    return gscast(b3ShapeCastHull(g, &in));
}

gs_phys_cast_output gs_phys_mesh_shape_cast(gs_phys_mesh m, gs_phys_float3 scale,
                                            gs_phys_float3_slice points, float radius,
                                            gs_phys_float3 translation, float max_fraction,
                                            uint8_t can_encroach) {
    PHYS_GEOMETRY(PHYS_MESH, m, "shape_cast", b3MeshData, phys_no_cast());
    phys_proxy p;
    b3ShapeCastInput in;
    if (!phys_shape_cast_input(&in, &p, points, radius, translation, max_fraction, can_encroach))
        return phys_no_cast();
    b3Mesh mesh = { g, b3v3(scale) };
    return gscast(b3ShapeCastMesh(&mesh, &in));
}

gs_phys_cast_output gs_phys_height_field_shape_cast(gs_phys_height_field h,
                                                    gs_phys_float3_slice points, float radius,
                                                    gs_phys_float3 translation, float max_fraction,
                                                    uint8_t can_encroach) {
    PHYS_GEOMETRY(PHYS_HEIGHT_FIELD, h, "shape_cast", b3HeightFieldData, phys_no_cast());
    phys_proxy p;
    b3ShapeCastInput in;
    if (!phys_shape_cast_input(&in, &p, points, radius, translation, max_fraction, can_encroach))
        return phys_no_cast();
    return gscast(b3ShapeCastHeightField(g, &in));
}

gs_phys_cast_output gs_phys_compound_shape_cast(gs_phys_compound c, gs_phys_float3_slice points,
                                                float radius, gs_phys_float3 translation,
                                                float max_fraction, uint8_t can_encroach) {
    PHYS_GEOMETRY(PHYS_COMPOUND, c, "shape_cast", b3CompoundData, phys_no_cast());
    phys_proxy p;
    b3ShapeCastInput in;
    if (!phys_shape_cast_input(&in, &p, points, radius, translation, max_fraction, can_encroach))
        return phys_no_cast();
    return gscast(b3ShapeCastCompound(g, &in));
}

gs_phys_distance_output gs_phys_shape_distance(gs_phys_float3_slice points_a, float radius_a,
                                               gs_phys_float3_slice points_b, float radius_b,
                                               gs_phys_transform b_in_a, uint8_t use_radii) {
    gs_phys_distance_output r;
    memset(&r, 0, sizeof r);
    /* Each proxy keeps its own copy of its points. */
    phys_proxy *pa = (phys_proxy *)malloc(2 * sizeof(phys_proxy));
    phys_proxy *pb = pa + 1;
    if (phys_make_proxy(pa, points_a, radius_a, "shape_distance") &&
        phys_make_proxy(pb, points_b, radius_b, "shape_distance")) {
        b3DistanceInput in;
        memset(&in, 0, sizeof in);
        in.proxyA = pa->proxy;
        in.proxyB = pb->proxy;
        in.transform = b3xf(b_in_a);
        in.useRadii = use_radii;
        b3SimplexCache cache = b3_emptyDistanceCache;
        b3DistanceOutput o = b3ShapeDistance(&in, &cache, NULL, 0);
        r.point_a = gsf3(o.pointA);
        r.point_b = gsf3(o.pointB);
        r.normal = gsf3(o.normal);
        r.distance = o.distance;
        r.iterations = o.iterations;
    }
    free(pa);
    return r;
}

gs_phys_cast_output gs_phys_shape_cast_pair(gs_phys_float3_slice points_a, float radius_a,
                                            gs_phys_float3_slice points_b, float radius_b,
                                            gs_phys_transform b_in_a, gs_phys_float3 translation_b,
                                            float max_fraction, uint8_t can_encroach) {
    gs_phys_cast_output r = phys_no_cast();
    phys_proxy *pa = (phys_proxy *)malloc(2 * sizeof(phys_proxy));
    phys_proxy *pb = pa + 1;
    if (phys_make_proxy(pa, points_a, radius_a, "shape_cast") &&
        phys_make_proxy(pb, points_b, radius_b, "shape_cast")) {
        b3ShapeCastPairInput in;
        memset(&in, 0, sizeof in);
        in.proxyA = pa->proxy;
        in.proxyB = pb->proxy;
        in.transform = b3xf(b_in_a);
        in.translationB = b3v3(translation_b);
        in.maxFraction = max_fraction;
        in.canEncroach = can_encroach;
        r = gscast(b3ShapeCast(&in));
    }
    free(pa);
    return r;
}

static b3Sweep b3sweep(gs_phys_sweep s) {
    b3Sweep r = { b3v3(s.local_center), b3v3(s.c1), b3v3(s.c2), b3q(s.q1), b3q(s.q2) };
    return r;
}

gs_phys_toi_output gs_phys_time_of_impact(gs_phys_float3_slice points_a, float radius_a,
                                          gs_phys_sweep sweep_a, gs_phys_float3_slice points_b,
                                          float radius_b, gs_phys_sweep sweep_b,
                                          float max_fraction) {
    gs_phys_toi_output r;
    memset(&r, 0, sizeof r);
    phys_proxy *pa = (phys_proxy *)malloc(2 * sizeof(phys_proxy));
    phys_proxy *pb = pa + 1;
    if (phys_make_proxy(pa, points_a, radius_a, "time_of_impact") &&
        phys_make_proxy(pb, points_b, radius_b, "time_of_impact")) {
        b3TOIInput in;
        memset(&in, 0, sizeof in);
        in.proxyA = pa->proxy;
        in.proxyB = pb->proxy;
        in.sweepA = b3sweep(sweep_a);
        in.sweepB = b3sweep(sweep_b);
        in.maxFraction = max_fraction;
        b3TOIOutput o = b3TimeOfImpact(&in);
        r.state = o.state;
        r.point = gsf3(o.point);
        r.normal = gsf3(o.normal);
        r.fraction = o.fraction;
        r.distance = o.distance;
    }
    free(pa);
    return r;
}

gs_phys_transform gs_phys_sweep_transform(gs_phys_sweep sweep, float time) {
    b3Sweep s = b3sweep(sweep);
    return gsxf(b3GetSweepTransform(&s, time));
}

/* The planes as Box3D's, which the solver writes its pushes into. */
static b3CollisionPlane *phys_planes(gs_phys_collision_plane_slice planes) {
    b3CollisionPlane *p = (b3CollisionPlane *)malloc((size_t)(planes.len ? planes.len : 1) *
                                                     sizeof(b3CollisionPlane));
    for (int64_t i = 0; i < planes.len; i++) {
        p[i].plane = b3plane(planes.data[i].plane);
        p[i].pushLimit = planes.data[i].push_limit;
        p[i].push = planes.data[i].push;
        p[i].clipVelocity = planes.data[i].clip_velocity;
    }
    return p;
}

int64_t gs_phys_solve_planes(gs_phys_float3 target_delta, gs_phys_collision_plane_slice planes,
                             gs_phys_float3 *delta) {
    phys_init();
    if (planes.len > INT32_MAX) {
        phys_misuse("physics::solve_planes: %lld planes", (long long)planes.len);
        *delta = target_delta;
        return 0;
    }
    b3CollisionPlane *p = phys_planes(planes);
    b3PlaneSolverResult r = b3SolvePlanes(b3v3(target_delta), p, (int)planes.len);
    for (int64_t i = 0; i < planes.len; i++) planes.data[i].push = p[i].push;
    free(p);
    *delta = gsf3(r.delta);
    return r.iterationCount;
}

gs_phys_float3 gs_phys_clip_vector(gs_phys_float3 vector, gs_phys_collision_plane_slice planes) {
    phys_init();
    if (planes.len > INT32_MAX) {
        phys_misuse("physics::clip_vector: %lld planes", (long long)planes.len);
        return vector;
    }
    b3CollisionPlane *p = phys_planes(planes);
    b3Vec3 r = b3ClipVector(b3v3(vector), p, (int)planes.len);
    free(p);
    return gsf3(r);
}
