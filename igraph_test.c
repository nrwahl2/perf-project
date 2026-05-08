#include <stdint.h>     // uint8_t
#include <stdio.h>      // printf
#include <stdlib.h>     // calloc, rand

#include <igraph.h>     // igraph_*, IGRAPH_*, VECTOR

#define VERTICES 4
#define BYTES_IN_KIB 1024
#define LOOP_ITER 100

static void
init_vertex_objects(uint8_t **objects, const size_t *object_sizes)
{
    // Initialize the data objects associated with each vertex
    for (int i = 0; i < VERTICES; i++) {
        const size_t size = BYTES_IN_KIB * object_sizes[i];

        // Ignore memory allocation errors for now
        objects[i] = calloc(size, sizeof(uint8_t));

        for (size_t j = 0; j < size; j++) {
            objects[i][j] = rand() / UINT8_MAX;
        }
    }
}

static void
process_step(igraph_integer_t start, igraph_integer_t end, uint8_t **objects,
             const size_t *object_sizes)
{
    size_t start_size = 0;
    size_t end_size = 0;
    size_t max_size = 0;

    start_size = BYTES_IN_KIB * object_sizes[start];
    end_size = BYTES_IN_KIB * object_sizes[end];
    max_size = (start_size >= end_size)? start_size : end_size;

    for (size_t j = 0; j < max_size; j++) {
        /* Add byte of end vertex's object to that of start vertex's object,
         * subject to modulus.
         *
         * The goal of accessing each byte of each object is to try to
         * ensure that we have to load the entire object into memory at some
         * point, and none of it gets optimized out.
         *
         * Alternatively, we could pick a random element from the start
         * array and a random element from the end array, for the addition.
         * However, this might make the array sizes less relevant and thus
         * make our placements in the memory hierarchy less important.
         */
        objects[start][j % start_size] += objects[end][j % end_size];
    }

    printf("%" IGRAPH_PRId " --> %" IGRAPH_PRId "\n", start, end);
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
 * - Use sparse adjacency matrix or adjacency list.
 * - Run multiple traversal loops in parallel to better exercise memory
 *   placement algorithms.
 * - Allow specifying number of loops and other such parameters for the driver
 *   code.
 * - Create matrices that correspond to interesting access/affinity patterns.
 * - Consider using undirected graphs instead, depending on how we want to
 *   define affinity.
 */
int
main(void)
{
    igraph_t graph;

    /* Weighted adjacency matrix: a transition matrix for a Markov chain.
     *
     * Technically this is the transpose of a transition matrix, since igraph
     * uses column-major storage.
     *
     * tmat[i][j] = probability of transition to vertex j given start at
     *              vertex i
     *
     * Each column sum will be normalized to 1, but for clarity, we define the
     * matrix with unit column sums.
     */
    const igraph_real_t tmat[VERTICES][VERTICES] = {
        { 0.0, 1.0, 0.2, 0.5 },
        { 0.2, 0.0, 0.3, 0.0 },
        { 0.8, 0.0, 0.3, 0.0 },
        { 0.0, 0.0, 0.2, 0.5 },
    };

    /*
    // Example: Get stuck in either {0, 2} or {1, 3}
    const igraph_real_t tmat[VERTICES][VERTICES] = {
        { 0.5, 0.0, 0.5, 0.0 },
        { 0.0, 0.5, 0.0, 0.5 },
        { 0.5, 0.0, 0.5, 0.0 },
        { 0.0, 0.5, 0.0, 0.5 },
    };
    */

    /* Data objects attached to each vertex.
     *
     * igraph supports arbitrary vertex attributes via IGRAPH_ATTRIBUTE_OBJECT,
     * but its C library provides no built-in way to attach them. It only
     * provides methods for setting and getting numeric, boolean, and string
     * attributes.
     *
     * Instead of trying to hack around that, store each vertex's associated
     * data in this 1-D array.
     */
    uint8_t *objects[VERTICES] = { NULL, };

    // Size of each vertex's attached data, in KiB
    const size_t object_sizes[VERTICES] = { 8, 1, 64, 32 };

    // C arrays use row-major storage, while igraph's matrix uses column-major
    const igraph_matrix_t tmat_transpose =
        igraph_matrix_view(*tmat, (sizeof(tmat[0]) / sizeof(tmat[0][0])),
                           (sizeof(tmat) / sizeof(tmat[0])));

    igraph_vector_t weights;
    igraph_vector_int_t vertices;
    igraph_vector_int_t edges;

    // Always start at vertex 0 for simplicity
    igraph_integer_t start = 0;

    igraph_setup();

    igraph_vector_init(&weights, 0);
    igraph_vector_int_init(&vertices, 0);
    igraph_vector_int_init(&edges, 0);

    igraph_weighted_adjacency(&graph, &tmat_transpose, IGRAPH_ADJ_DIRECTED,
                              &weights, IGRAPH_LOOPS_ONCE);
    init_vertex_objects(objects, object_sizes);

    /* When igraph_weighted_adjacency() returns, 'weights' will typically have
     * more capacity allocated than what it uses. We may optionally free any
     * unused capacity to save memory, although in most applications this
     * is not necessary.
     *
     * This is from the igraph example code, but it may not be desirable for our
     * use case.
     */
    igraph_vector_resize_min(&weights);

    // Walk one step at a time, to try to prevent the prefetcher from "helping"
    for (int i = 0; i < LOOP_ITER; i++) {
        igraph_integer_t end = 0;

        igraph_random_walk(&graph, &weights, &vertices, &edges, start,
                           IGRAPH_OUT, 1, IGRAPH_RANDOM_WALK_STUCK_ERROR);
        end = VECTOR(vertices)[1];

        process_step(start, end, objects, object_sizes);
        start = end;
    }

    for (int i = 0; i < VERTICES; i++) {
        free(objects[i]);
    }

    igraph_vector_int_destroy(&vertices);
    igraph_vector_int_destroy(&edges);
    igraph_vector_destroy(&weights);
    igraph_destroy(&graph);

    return 0;
}
