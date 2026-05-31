#include <errno.h>      // errno
#include <stdint.h>     // SIZE_MAX, uint8_t
#include <stdio.h>      // printf
#include <stdlib.h>     // calloc
#include <string.h>     // strerror

#include <igraph.h>     // igraph_*, IGRAPH_*, VECTOR

// Enables debug logging to stdout if defined
#define DEBUG

#define INPUT_FILENAME "graphs/connected.graphml"

#define BYTES_PER_CHUNK (1 << 20)
#define LOOP_ITER 10

#define ATTR_PATTERN "pattern"
#define ATTR_SIZE "size"
#define ATTR_WEIGHT "weight"

#ifdef DEBUG
#define debug_printf(...) printf(__VA_ARGS__)
#else
#define debug_printf(...)
#endif

static inline size_t
vertex_size(const igraph_t *graph, igraph_int_t vid)
{
    return BYTES_PER_CHUNK * (size_t) VAN(graph, ATTR_SIZE, vid);
}

static uint8_t *
init_vertex_object(const igraph_t *graph, igraph_int_t index)
{
    const size_t size = vertex_size(graph, index);
    uint8_t *obj = calloc(size, sizeof(uint8_t));

    if (obj == NULL) {
        fprintf(stderr,
                "Failed to allocate %zu bytes for vertex %" IGRAPH_PRId
                ": %s\n", size, index, strerror(errno));
        return NULL;
    }

    for (size_t j = 0; j < size; j++) {
        obj[j] = igraph_rng_get_integer(igraph_rng_default(), 0, UINT8_MAX);
    }

    return obj;
}

static int
init_vertex_objects(const igraph_t *graph, uint8_t **objects)
{
    const igraph_int_t vcount = igraph_vcount(graph);

    // Initialize the data objects associated with each vertex
    for (igraph_int_t i = 0; i < vcount; i++) {
        objects[i] = init_vertex_object(graph, i);

        if (objects[i] == NULL) {
            return -1;
        }
    }

    return 0;
}

static bool
vertex_access_random(const igraph_t *graph, igraph_int_t vid)
{
    const char *pattern = VAS(graph, ATTR_PATTERN, vid);

    /* We don't currently validate that the ATTR_PATTERN attribute is set to
     * either "random" or "strided". If it's not "random", we use a strided
     * access pattern.
     */
    return (pattern != NULL) && (strcmp(pattern, "random") == 0);
}

static size_t
get_random_index(size_t max_index)
{
    igraph_rng_t *rng = igraph_rng_default();

#if (SIZE_MAX > IGRAPH_INTEGER_MAX)
    if (max_index > IGRAPH_INTEGER_MAX) {
        /* Shift unsigned to signed range by subtracting IGRAPH_INTEGER_MAX from
         * both endpoints.
         *
         * This assumes that SIZE_MAX <= IGRAPH_UINT_MAX, which should always be
         * true in practice.
         */
        const igraph_uint_t lower_u = 0;
        const igraph_uint_t upper_u = max_index;
        const igraph_int_t lower_i = lower_u - IGRAPH_INTEGER_MAX;
        const igraph_int_t upper_i = upper_u - IGRAPH_INTEGER_MAX;

        // Shift the random integer back to the desired unsigned range
        return ((size_t) igraph_rng_get_integer(rng, lower_i, upper_i))
               + IGRAPH_INTEGER_MAX;
    }
#endif  // (SIZE_MAX >= IGRAPH_INTEGER_MAX)

    return igraph_rng_get_integer(rng, 0, max_index);
}

static void
process_step(const igraph_t *graph, igraph_int_t start, igraph_int_t end,
             uint8_t **objects)
{
    const size_t start_size = vertex_size(graph, start);
    const size_t end_size = vertex_size(graph, end);
    const size_t max_size = (start_size >= end_size)? start_size : end_size;

    const bool start_random = vertex_access_random(graph, start);
    const bool end_random = vertex_access_random(graph, end);

    uint8_t *start_obj = objects[start];
    uint8_t *end_obj = objects[end];

    for (size_t j = 0; j < max_size; j++) {
        size_t start_idx = j % start_size;
        size_t end_idx = j % end_size;

        if (start_random) {
            start_idx = get_random_index(start_size);
        }

        if (end_random) {
            end_idx = get_random_index(end_size);
        }

        /* Add an element of the end vertex's object to an element of the start
         * vertex's object, modulo the start object's size.
         *
         * The goal of accessing each element of each object is to try to ensure
         * that we have to load the entire object into memory at some point, so
         * that none of it gets optimized out.
         */
        start_obj[start_idx] += end_obj[end_idx];
    }

    debug_printf("%" IGRAPH_PRId " --> %" IGRAPH_PRId "\n", start, end);
}

/* We start with a stochastic adjacency matrix for a directed weighted graph.
 * The weights roughly indicate the memory affinity between nodes. We treat the
 * graph as a Markov chain. At each time step, we perform one transition from
 * the current node, by randomly sampling from its adjacent nodes using the edge
 * weights as probabilities.
 *
 * The idea is that the program will access data objects (graph nodes) in
 * accordance with the specified memory affinity pattern.
 *
 * @TODO
 * - Consider using different kinds of operations (currently only addition), as
 *   a way to further specify which memory accesses need to be faster.
 * - Run multiple traversal loops in parallel to better exercise memory
 *   placement algorithms.
 * - Allow specifying number of loops and other such parameters for the driver
 *   code.
 * - Create matrices that correspond to interesting access/affinity patterns.
 */
int
main(void)
{
    int rc = 0;
    igraph_error_t igraph_errno = IGRAPH_SUCCESS;

    igraph_t graph = { 0, };
    FILE *input_file = NULL;

    /* Data objects attached to each vertex.
     *
     * igraph supports arbitrary vertex attributes via IGRAPH_ATTRIBUTE_OBJECT,
     * but its C library provides no built-in way to attach them. It only
     * provides methods for setting and getting numeric, boolean, and string
     * attributes. Manipulating attributes of type IGRAPH_ATTRIBUTE_OBJECT
     * requires a custom attribute table, which would be a pain to create.
     *
     * Instead, store each vertex's associated data in this 1-D array. This also
     * gives us explicit control over how to allocate and place each object in a
     * heterogeneous memory hierarchy.
     */
    uint8_t **objects = NULL;

    igraph_vector_t weights = { 0, };
    igraph_vector_int_t vertices = { 0, };
    igraph_vector_int_t edges = { 0, };

    igraph_int_t vcount = 0;

    // Always start at vertex 0 for simplicity
    igraph_int_t start = 0;

    igraph_setup();
    igraph_set_attribute_table(&igraph_cattribute_table);

    // Comment the below line out for random results among runs
    igraph_rng_seed(igraph_rng_default(), 0);

    igraph_vector_init(&weights, 0);
    igraph_vector_int_init(&vertices, 0);
    igraph_vector_int_init(&edges, 0);

    input_file = fopen(INPUT_FILENAME, "r");
    if (input_file == NULL) {
        fprintf(stderr, "Failed to open input file " INPUT_FILENAME ": %s\n",
                strerror(errno));
        rc = 1;
        goto done;
    }

    igraph_errno = igraph_read_graph_graphml(&graph, input_file, 0);
    if (igraph_errno != IGRAPH_SUCCESS) {
        fprintf(stderr, "Failed to read " INPUT_FILENAME " as GraphML: %s\n",
                igraph_strerror(igraph_errno));
        rc = 1;
        goto done;
    }

    igraph_errno = EANV(&graph, ATTR_WEIGHT, &weights);
    if (igraph_errno != IGRAPH_SUCCESS) {
        fprintf(stderr, "Failed to get edge weights from graph: %s\n",
                igraph_strerror(igraph_errno));
        rc = 1;
        goto done;
    }

    vcount = igraph_vcount(&graph);

    objects = calloc(vcount, sizeof(uint8_t *));
    if (objects == NULL) {
        fprintf(stderr, "Failed to allocate array for vertex objects: %s\n",
                strerror(errno));
        rc = 1;
        goto done;
    }

    if (init_vertex_objects(&graph, objects) != 0) {
        // Error already logged
        rc = 1;
        goto done;
    }

    // Walk one step at a time, to try to prevent the prefetcher from "helping"
    for (int i = 0; i < LOOP_ITER; i++) {
        igraph_int_t end = 0;

        igraph_random_walk(&graph, &weights, &vertices, &edges, start,
                           IGRAPH_OUT, 1, IGRAPH_RANDOM_WALK_STUCK_ERROR);
        end = VECTOR(vertices)[1];

        process_step(&graph, start, end, objects);
        start = end;
    }

done:
    if (input_file != NULL) {
        fclose(input_file);
    }

    if (objects != NULL) {
        for (int i = 0; i < vcount; i++) {
            free(objects[i]);
        }

        free(objects);
    }

    igraph_vector_destroy(&weights);
    igraph_vector_int_destroy(&vertices);
    igraph_vector_int_destroy(&edges);
    igraph_destroy(&graph);

    return rc;
}
