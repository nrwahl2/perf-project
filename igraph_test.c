#include <stdio.h>      // printf

#include <igraph.h>     // igraph_*, IGRAPH_*, VECTOR

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
    const igraph_real_t data[4][4] = { { 0.0, 1.0, 0.2, 0.5 },
                                       { 0.2, 0.0, 0.3, 0.0 },
                                       { 0.8, 0.0, 0.3, 0.0 },
                                       { 0.0, 0.0, 0.2, 0.5 } };

    /*
    // Get stuck in either {0, 2} or {1, 3}
    const igraph_real_t data[4][4] = { { 0.5, 0.0, 0.5, 0.0 },
                                       { 0.0, 0.5, 0.0, 0.5 },
                                       { 0.5, 0.0, 0.5, 0.0 },
                                       { 0.0, 0.5, 0.0, 0.5 } };
                                       */

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
