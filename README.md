# Memory affinity micro-benchmarks

## Overview

This is a micro-benchmark suite for placement of multiple data objects within a
heterogeneous memory hierarchy. MemFriend, part of
[MemGaze](https://github.com/pnnl/memgaze) from PNNL, offers "spatial and
temporal analysis that captures affinity (access correlation) between pairs of
memory locations."

Our hypothesis is that MemFriend's affinity analysis can inform the placement of
objects within the memory hierarchy, in a way that outperforms more naive
placement approaches. This repository provides a way to test that hypothesis.

We model a memory access pattern using a weighted directed graph. In particular,
we model the access pattern as a Markov chain whose transition matrix is the
graph and whose states are the vertices. We randomly walk the graph for a set
number of iterations, accessing the data objects according to this pattern.

* Each vertex is associated with a data object that must be placed in the memory
  hierarchy. Currently, each data object is an array of `uint8_t`.
* Visiting a vertex triggers accessing that vertex's data object.
* The weight of an edge from vertex **A** to vertex **B** is the probability of
  visiting **B** next, given that **A** was visited most recently. If there is
  no edge from **A** to **B**, then the probability of **B** given **A** is
  zero.
* The weights of the edges originating from a given vertex sum to one.

An input graph is read from a [GraphML](http://graphml.graphdrawing.org/) file.
Some provided input graphs are stored in the `graphs` subdirectory. Currently,
the input file path is hard-coded via the `INPUT_FILENAME` macro. To choose a
different input file path, change the macro definition.

The `<graph>` element in the GraphML file must have `edgedefault="directed"`.

We expect the following attributes in the GraphML file:

* The `size` vertex attribute (type `int`) determines the size (in chunks) of
  each vertex's associated data object. This must be set for each vertex.
  - The chunk size is currently hard-coded via the `BYTES_PER_CHUNK` macro.
* The `weight` edge attribute (type `double`) determines the weight of each
  edge. This must be set for each edge. As mentioned previously, the weights of
  the edges originating from a given vertex should sum to one.

The number of graph traversal steps is currently hard-coded via the `LOOP_ITER`
macro.

At each traversal step, we access the entire data objects for the start and end
vertices. For example, suppose we start at vertex **A** and step to vertex
**B**. Then we access each element of **B**'s object in order and add it to the
corresponding element of **A**. This ensures that the entire object must be
loaded into memory at some point during the step; no part of the access can be
optimized away. If one object is larger than the other, then we start back at
the first element of the smaller object when we would overflow its array bounds.

Currently, the elements of each object are accessed in order. An option for
random access within an object is planned.

Support for running with multiple threads and OpenMP is also planned.

## Dependencies

* [CMake](https://cmake.org/) 3.1.8 or later
* [igraph](https://igraph.org/) 1.0.0 or later

## Build and installation

From the top level of the source directory:

```
$ mkdir build
$ cd build
$ cmake ..
$ cmake --build .
```

Then to run the program:

```
$ build/igraph_test
```

## Micro-benchmark procedure

1. Create an input graph representing the memory affinity pattern that you wish
   to model, or choose one from the `graphs` subdirectory.
2. Define the `INPUT_FILENAME` macro to the path of the input graph file.
3. Run your desired algorithm to decide where to place the objects in the memory
   hierarchy based on their affinities.
4. In the `init_vertex_objects()` function, replace the `calloc()` calls as
   appropriate for each vertex, to place each data object in the desired
   location. For example, you might use `numa_alloc_onnode()` to allocate an
   object on a specific NUMA node.
5. Compare run times using various placement strategies.
