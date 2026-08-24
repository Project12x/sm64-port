/* Saturn-aware terrain decimation (S7.1).
 *
 * Reduces a level's render mesh before quad pairing, so the whole downstream
 * pipeline -- pairing, clustering, BSP, admission, VDP1 emission -- sees fewer
 * primitives. Measured motivation: master-only work is ~70% of the frame, and
 * primitive count is the one lever that cuts BOTH master-only spans at once
 * (fewer clusters to admit, fewer commands to emit). Quad-pairing tolerance was
 * measured first and rejected: 4x tolerance bought 5.9% because the ceiling is
 * geometric, not tunable (docs/saturn/evidence/reports/quad-pairing-headroom.md).
 * Decimation removes triangles instead of merging them, so that ceiling does
 * not apply to it.
 *
 * Upstream: meshoptimizer, MIT, github.com/zeux/meshoptimizer, vendored at
 * work/upstream/meshoptimizer, HEAD 97bbdce (2026-08-08), verified live
 * 2026-08-23 (pushed 2026-08-21, 8258 stars). Reuse mode: dependency -- this
 * file calls the published API, it does not copy or port upstream code.
 * meshopt_simplifyWithAttributes is used rather than meshopt_simplify for two
 * Saturn-specific reasons:
 *
 *   - vertex_lock pins vertices that sit on a material boundary. Terrain here
 *     is textured per-material with no shared atlas, so collapsing an edge
 *     across a seam would drag one material's UVs onto another's triangles.
 *     Locking is what keeps decimation inside a material.
 *   - UVs are passed as weighted attributes, so the error metric penalises
 *     texture stretch, not just silhouette change. VDP1 maps textures affinely
 *     across a quad, which makes it less forgiving of UV distortion than the
 *     N64 source this geometry came from.
 *
 * NOT touched: collision. levels/<x>/areas/<n>/collision.inc.c is a separate
 * source from geo.inc.c and never passes through this tool, so decimating the
 * render mesh cannot move a floor out from under Mario.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "meshoptimizer.h"

namespace {

/* Minimal JSON reading: the input is this project's own generated mesh IR, not
 * arbitrary input, so a full parser would be weight without benefit. Every
 * accessor below fails closed rather than guessing. */
struct Json {
    std::string text;

    bool load(const char *path)
    {
        FILE *file = fopen(path, "rb");
        if (file == NULL) return false;
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (size <= 0) { fclose(file); return false; }
        text.resize((size_t)size);
        size_t read = fread(&text[0], 1, (size_t)size, file);
        fclose(file);
        return read == (size_t)size;
    }
};

size_t skip_space(const std::string &s, size_t i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' ||
                            s[i] == '\t'))
        i++;
    return i;
}

/* Finds "key" at the given nesting depth from `from`, returning the index just
 * past the following colon. */
size_t find_key(const std::string &s, const char *key, size_t from = 0)
{
    std::string needle = std::string("\"") + key + "\"";
    size_t at = s.find(needle, from);
    if (at == std::string::npos) return std::string::npos;
    at = skip_space(s, at + needle.size());
    if (at >= s.size() || s[at] != ':') return std::string::npos;
    return skip_space(s, at + 1);
}

/* Reads a flat array of integer arrays: [[1,2,3],[4,5,6]] */
bool read_int_tuples(const std::string &s, size_t at, size_t arity,
                     std::vector<long> &out)
{
    if (at >= s.size() || s[at] != '[') return false;
    at++;
    while (true) {
        at = skip_space(s, at);
        if (at >= s.size()) return false;
        if (s[at] == ']') return true;
        if (s[at] == ',') { at++; continue; }
        if (s[at] != '[') return false;
        at++;
        for (size_t component = 0; component < arity; component++) {
            at = skip_space(s, at);
            char *end = NULL;
            long value = strtol(s.c_str() + at, &end, 10);
            if (end == s.c_str() + at) return false;
            out.push_back(value);
            at = (size_t)(end - s.c_str());
            at = skip_space(s, at);
            if (component + 1 < arity) {
                if (at >= s.size() || s[at] != ',') return false;
                at++;
            }
        }
        at = skip_space(s, at);
        if (at >= s.size() || s[at] != ']') return false;
        at++;
    }
}

} // namespace

int main(int argc, char **argv)
{
    const char *input = NULL, *output = NULL, *report_path = NULL;
    double ratio = 0.75;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) output = argv[++i];
        else if (strcmp(argv[i], "--report") == 0 && i + 1 < argc) report_path = argv[++i];
        else if (strcmp(argv[i], "--ratio") == 0 && i + 1 < argc) ratio = atof(argv[++i]);
        else { fprintf(stderr, "unknown argument: %s\n", argv[i]); return 2; }
    }
    if (input == NULL || output == NULL) {
        fprintf(stderr, "usage: --input mesh_ir.json --output out.json "
                        "[--report r.json] [--ratio 0.75]\n");
        return 2;
    }
    if (!(ratio > 0.0 && ratio <= 1.0)) {
        fprintf(stderr, "ratio must be in (0, 1]\n");
        return 2;
    }

    Json doc;
    if (!doc.load(input)) {
        fprintf(stderr, "cannot read %s\n", input);
        return 1;
    }

    std::vector<long> positions;
    size_t at = find_key(doc.text, "positions");
    if (at == std::string::npos || !read_int_tuples(doc.text, at, 3, positions)) {
        fprintf(stderr, "mesh IR has no readable integer \"positions\"\n");
        return 1;
    }
    const size_t vertex_count = positions.size() / 3;

    std::vector<long> uvs;
    at = find_key(doc.text, "uv");
    const bool has_uv = at != std::string::npos &&
                        read_int_tuples(doc.text, at, 2, uvs) &&
                        uvs.size() / 2 == vertex_count;

    /* Triangles carry an "indices" array each; collect them in document order
     * together with the material each one belongs to. */
    std::vector<unsigned int> indices;
    std::vector<int> triangle_material;
    size_t cursor = find_key(doc.text, "triangles");
    if (cursor == std::string::npos) {
        fprintf(stderr, "mesh IR has no \"triangles\"\n");
        return 1;
    }
    while (true) {
        size_t index_at = find_key(doc.text, "indices", cursor);
        if (index_at == std::string::npos) break;
        size_t material_at = find_key(doc.text, "material", cursor);
        long material = 0;
        if (material_at != std::string::npos && material_at < index_at)
            material = strtol(doc.text.c_str() + material_at, NULL, 10);
        std::vector<long> tri;
        if (!read_int_tuples(doc.text, index_at - 1, 3, tri)) {
            /* read_int_tuples expects an array-of-arrays; a bare [a,b,c] is
             * read by pointing one character earlier so the outer bracket is
             * synthesised by the caller's own '[' check. Fall back to a direct
             * three-integer read. */
            const char *p = doc.text.c_str() + index_at;
            if (*p != '[') { cursor = index_at; continue; }
            p++;
            for (int k = 0; k < 3; k++) {
                char *end = NULL;
                tri.push_back(strtol(p, &end, 10));
                p = end;
                while (*p == ',' || *p == ' ') p++;
            }
        }
        if (tri.size() >= 3) {
            for (int k = 0; k < 3; k++)
                indices.push_back((unsigned int)tri[(size_t)k]);
            triangle_material.push_back((int)material);
        }
        cursor = index_at + 1;
    }
    if (indices.empty()) {
        fprintf(stderr, "mesh IR produced no triangles\n");
        return 1;
    }
    const size_t source_triangles = indices.size() / 3;

    /* Positions as float for the metric; the source is integral world space. */
    std::vector<float> vertex_positions(vertex_count * 3);
    for (size_t i = 0; i < vertex_count * 3; i++)
        vertex_positions[i] = (float)positions[i];

    std::vector<float> vertex_attributes;
    float attribute_weights[2] = {0.0f, 0.0f};
    size_t attribute_count = 0;
    if (has_uv) {
        vertex_attributes.resize(vertex_count * 2);
        for (size_t i = 0; i < vertex_count * 2; i++)
            vertex_attributes[i] = (float)uvs[i];
        /* Weight relative to world scale: BOB spans ~16k world units while UVs
         * are S10.5 fixed point, so an unweighted UV term would dominate the
         * position term by orders of magnitude and freeze the mesh. */
        attribute_weights[0] = attribute_weights[1] = 1.0f / 512.0f;
        attribute_count = 2;
    }

    /* Lock every vertex that more than one material touches. Terrain is
     * textured per material with no shared atlas, so a collapse across a seam
     * drags one material's UVs onto another's triangles. */
    std::vector<std::set<int> > vertex_materials(vertex_count);
    for (size_t t = 0; t < source_triangles; t++)
        for (int k = 0; k < 3; k++)
            vertex_materials[indices[t * 3 + (size_t)k]].insert(
                triangle_material[t]);
    std::vector<unsigned char> vertex_lock(vertex_count, 0);
    size_t locked = 0;
    for (size_t v = 0; v < vertex_count; v++) {
        if (vertex_materials[v].size() > 1) {
            vertex_lock[v] = 1;
            locked++;
        }
    }

    const size_t target_index_count =
        (size_t)((double)indices.size() * ratio) / 3 * 3;
    std::vector<unsigned int> destination(indices.size());
    float result_error = 0.0f;
    const size_t result_count = meshopt_simplifyWithAttributes(
        &destination[0], &indices[0], indices.size(), &vertex_positions[0],
        vertex_count, sizeof(float) * 3,
        attribute_count ? &vertex_attributes[0] : NULL,
        attribute_count ? sizeof(float) * 2 : 0,
        attribute_count ? attribute_weights : NULL, attribute_count,
        &vertex_lock[0], target_index_count, 1e9f, 0, &result_error);
    destination.resize(result_count);

    const size_t kept_triangles = result_count / 3;
    fprintf(stderr,
            "decimate: %zu -> %zu triangles (ratio %.3f requested, %.3f "
            "achieved), %zu/%zu vertices locked on material seams, error %.4f\n",
            source_triangles, kept_triangles, ratio,
            (double)kept_triangles / (double)source_triangles, locked,
            vertex_count, (double)result_error);

    if (report_path != NULL) {
        FILE *r = fopen(report_path, "wb");
        if (r == NULL) { fprintf(stderr, "cannot write %s\n", report_path); return 1; }
        fprintf(r,
                "{\"schema\":\"saturn-terrain-decimation-v1\",\"ratio_requested\":%.6f,"
                "\"source_triangles\":%zu,\"kept_triangles\":%zu,"
                "\"ratio_achieved\":%.6f,\"vertices\":%zu,"
                "\"vertices_locked_material_seam\":%zu,\"result_error\":%.6f,"
                "\"attribute_metric\":%s}\n",
                ratio, source_triangles, kept_triangles,
                (double)kept_triangles / (double)source_triangles, vertex_count,
                locked, (double)result_error, has_uv ? "\"uv\"" : "null");
        fclose(r);
    }

    /* Emit the surviving triangle index list. The caller rebuilds the mesh IR
     * around it: this tool deliberately does not rewrite the whole document,
     * so material/tile/source metadata stays owned by the Python bake that
     * understands it. */
    FILE *out = fopen(output, "wb");
    if (out == NULL) { fprintf(stderr, "cannot write %s\n", output); return 1; }
    fprintf(out, "{\"schema\":\"saturn-terrain-decimation-indices-v1\","
                 "\"triangles\":[");
    for (size_t t = 0; t < kept_triangles; t++) {
        fprintf(out, "%s[%u,%u,%u]", t ? "," : "", destination[t * 3],
                destination[t * 3 + 1], destination[t * 3 + 2]);
    }
    fprintf(out, "]}\n");
    fclose(out);
    return 0;
}
