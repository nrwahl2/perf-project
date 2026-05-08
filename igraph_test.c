#include <stdint.h>     // uint8_t
#include <stdio.h>      // printf

#include <igraph.h>     // igraph_*, IGRAPH_*, VECTOR

#define VERTICES 4

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
 * - Alter the size of graph nodes by attaching data blobs to them.
 *   * Should the nodes be irregularly sized?
 *
 * - Perform some operation (e.g., addition) on the attached data blobs, to
 *   ensure that the data gets used and the accesses aren't optimized out.
 *   * Using different kinds of operations could also be a way to specify which
 *     accesses need to be faster.
 *
 * - Use sparse adjacency matrix or adjacency list.
 * - Run multiple traversal loops in parallel to better exercise memory
 *   placement algorithms.
 * - Allow specifying number of loops and other such parameters for the driver
 *   code.
 * - Create matrices that correspond to interesting access/affinity patterns.
 * - Consider using undirected graphs instead, depending on how we want to
 *   define affinity.
 */
int main(void)
{
    igraph_t graph;

    /* Weighted adjacency matrix: a transition matrix for a Markov chain.
     *
     * Technically this is the transpose of a transition matrix, since igraph
     * uses column-major storage.
     *
     * data[i][j] = probability of transition to vertex j given start at
     *              vertex i
     *
     * Each column sum will be normalized to 1, but for clarity, we define the
     * matrix with unit column sums.
     */
    const igraph_real_t data[VERTICES][VERTICES] = {
        { 0.0, 1.0, 0.2, 0.5 },
        { 0.2, 0.0, 0.3, 0.0 },
        { 0.8, 0.0, 0.3, 0.0 },
        { 0.0, 0.0, 0.2, 0.5 },
    };

    /*
    // Example: Get stuck in either {0, 2} or {1, 3}
    const igraph_real_t data[4][4] = { { 0.5, 0.0, 0.5, 0.0 },
                                       { 0.0, 0.5, 0.0, 0.5 },
                                       { 0.5, 0.0, 0.5, 0.0 },
                                       { 0.0, 0.5, 0.0, 0.5 } };
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
    void *objects[VERTICES] = { NULL, };

    // Size of each vertex's attached data, in KiB
    const size_t object_sizes[VERTICES] = { 8, 1, 64, 32 };

    /* C arrays use row-major storage, while igraph's matrix uses column-major.
     * The matrix 'mat' will be the transpose of 'data'.
     */
    const igraph_matrix_t mat =
        igraph_matrix_view(*data, (sizeof(data[0]) / sizeof(data[0][0])),
                           (sizeof(data) / sizeof(data[0])));

    igraph_vector_t weights;
    igraph_vector_int_t vertices;
    igraph_vector_int_t edges;
    igraph_integer_t start = 0;

    igraph_setup();

    igraph_vector_init(&weights, 0);
    igraph_vector_int_init(&vertices, 0);
    igraph_vector_int_init(&edges, 0);

    igraph_weighted_adjacency(&graph, &mat, IGRAPH_ADJ_DIRECTED, &weights,
                              IGRAPH_LOOPS_ONCE);

    for (int i = 0; i < VERTICES; i++) {
        // Ignore memory allocation errors for now
        objects[i] = calloc(1024 * object_sizes[i], sizeof(uint8_t));
    }

    /* TODO Access each vertex's data when we access the vertex. Possibly do
     * some operation to ensure that the access doesn't get optimized out.
     */

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
    for (int i = 0; i < 100; i++) {
        igraph_random_walk(&graph, &weights, &vertices, &edges, start,
                           IGRAPH_OUT, 1, IGRAPH_RANDOM_WALK_STUCK_ERROR);

        start = VECTOR(vertices)[1];
        printf("%" IGRAPH_PRId " --> %" IGRAPH_PRId "\n", VECTOR(vertices)[0],
               VECTOR(vertices)[1]);
    }

    igraph_vector_int_destroy(&vertices);
    igraph_vector_int_destroy(&edges);
    igraph_vector_destroy(&weights);
    igraph_destroy(&graph);

    return 0;
}
