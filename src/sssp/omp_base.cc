#include "sssp.h"
#include <omp.h>
#include "timer.h"
#include "platform_atomics.h"
#define SSSP_VARIANT "openmp"
/*
GARDINIA Benchmark Suite
Kernel: Single-source Shortest Paths (SSSP)
Author: Xuhao Chen

Returns array of distances for all vertices from given source vertex

This SSSP implementation makes use of the ∆-stepping algorithm [1]. The type
used for weights and distances (WeightT) is typedefined in benchmark.h. The
delta parameter (-d) should be set for each input graph.

The bins of width delta are actually all thread-local and of type std::vector
so they can grow but are otherwise capacity-proportional. Each iteration is
done in two phases separated by barriers. In the first phase, the current
shared bin is processed by all threads. As they find vertices whose distance
they are able to improve, they add them to their thread-local bins. During this
phase, each thread also votes on what the next bin should be (smallest
non-empty bin). In the next phase, each thread copies their selected
thread-local bin into the shared bin.

Once a vertex is added to a bin, it is not removed, even if its distance is
later updated and it now appears in a lower bin. We find ignoring vertices if
their current distance is less than the min distance for the bin to remove
enough redundant work that this is faster than removing the vertex from older
bins.

[1] Ulrich Meyer and Peter Sanders. "δ-stepping: a parallelizable shortest path
	algorithm." Journal of Algorithms, 49(1):114–152, 2003.
*/

void SSSPSolver(int m, int nnz, int *row_offsets, int *column_indices, DistT *weight, DistT *dist) {
	printf("Launching OpenMP SSSP solver...\n");
	omp_set_num_threads(8);
	int num_threads = 1;
#pragma omp parallel
	{
		num_threads = omp_get_num_threads();
	}
	printf("Launching %d threads...\n", num_threads);
	Timer t;
	int source = 0;
	DistT delta = 1;
	//for (int i = 0; i < m; i ++) dist[i] = kDistInf;
	dist[source] = 0;
	vector<int> frontier(nnz);
	// two element arrays for double buffering curr=iter&1, next=(iter+1)&1
	size_t shared_indexes[2] = {0, kDistInf};
	size_t frontier_tails[2] = {1, 0}; 
	frontier[0] = source;
	//printf("kDistInf=%d\n", kDistInf);
	t.Start();
#pragma omp parallel
	{
		vector<vector<int> > local_bins(0);
		size_t iter = 0;
		while (static_cast<DistT>(shared_indexes[iter&1]) != kDistInf) {
			size_t &curr_bin_index = shared_indexes[iter&1];
			size_t &next_bin_index = shared_indexes[(iter+1)&1];
			size_t &curr_frontier_tail = frontier_tails[iter&1];
			size_t &next_frontier_tail = frontier_tails[(iter+1)&1];
			#pragma omp single
			printf("\titer = %d, frontier_size = %d.\n", iter, curr_frontier_tail);
#pragma omp for nowait schedule(dynamic, 64)
			for (size_t i = 0; i < curr_frontier_tail; i ++) {
				int src = frontier[i];
				if (dist[src] >= delta * static_cast<DistT>(curr_bin_index)) {
					int row_begin = row_offsets[src];
					int row_end = row_offsets[src + 1];
					for (int offset = row_begin; offset < row_end; offset ++) {
						int dst = column_indices[offset];
						DistT old_dist = dist[dst];
						DistT new_dist = dist[src] + weight[offset];
						if (new_dist < old_dist) {
							bool changed_dist = true;
							while (!compare_and_swap(dist[dst], old_dist, new_dist)) {
								old_dist = dist[dst];
								if (old_dist <= new_dist) {
									changed_dist = false;
									break;
								}
							}
							if (changed_dist) {
								size_t dest_bin = new_dist/delta;
								if (dest_bin >= local_bins.size()) {
									//printf("\tdest_bin = %d, old_dist=%d, new_dist=%d.\n", dest_bin, old_dist, new_dist);
									local_bins.resize(dest_bin+1);
								}
								local_bins[dest_bin].push_back(dst);
							}
						}
					}
				}
			}
			for (size_t i = curr_bin_index; i < local_bins.size(); i ++) {
				if (!local_bins[i].empty()) {
#pragma omp critical
					next_bin_index = min(next_bin_index, i);
					break;
				}
			}
#pragma omp barrier
#pragma omp single nowait
			{
				curr_bin_index = kDistInf;
				curr_frontier_tail = 0;
			}
			if (next_bin_index < local_bins.size()) {
				size_t copy_start = fetch_and_add(next_frontier_tail,
						local_bins[next_bin_index].size());
				copy(local_bins[next_bin_index].begin(),
						local_bins[next_bin_index].end(), frontier.data() + copy_start);
				local_bins[next_bin_index].resize(0);
			}
			iter++;
#pragma omp barrier
		}
	}
	t.Stop();
	printf("\truntime [%s] = %f ms.\n", SSSP_VARIANT, t.Millisecs());
	return;
}
