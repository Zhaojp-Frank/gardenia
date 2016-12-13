#define BFS_VARIANT "gather"
#include <cub/cub.cuh>
#include "cuda_launch_config.hpp"
#include "cutil_subset.h"
#define EPSILON 0.001
#define MAX_ITER 19
const float kDamp = 0.85;
#define BLKSIZE 256

__global__ void initialize(int m, float *pr, bool *active) {
	unsigned id = blockIdx.x * blockDim.x + threadIdx.x;
	if (id < m) {
		pr[id] = 1.0f / (float)m;
		//active[id] = true;
	}
}

__global__ void process(int m, float *pr, int *degree, float *outgoing_contrib, bool *active) {
	unsigned tid = blockIdx.x * blockDim.x + threadIdx.x;
	int total_inputs = (m - 1) / (gridDim.x * blockDim.x) + 1;
	for (int src = tid; total_inputs > 0; src += blockDim.x * gridDim.x, total_inputs--) {
		if (src < m) {
		//if (src < m && active[src]) {
			outgoing_contrib[src] = kDamp * pr[src] / degree[src];
		}
	}
}

__global__ void gather(int m, int *row_offsets, int *column_indices, float *pr, float *contrib, float *diff, bool *active) {
	unsigned tid = blockIdx.x * blockDim.x + threadIdx.x;
	typedef cub::BlockReduce<float, BLKSIZE> BlockReduce;
	__shared__ typename BlockReduce::TempStorage temp_storage;
	int total_inputs = (m - 1) / (gridDim.x * blockDim.x) + 1;
	float local_diff = 0;
	for (int src = tid; total_inputs > 0; src += blockDim.x * gridDim.x, total_inputs--) {
		if (src < m) {
		//if (src < m && active[src]) {
			unsigned row_begin = row_offsets[src];
			unsigned row_end = row_offsets[src + 1];
			float incoming_total = 0;
			for (unsigned offset = row_begin; offset < row_end; ++ offset) {
				int dst = column_indices[offset];
				incoming_total += contrib[dst];
			}
			float old_pr = pr[src];
			pr[src] = ((1.0f - kDamp) / (float)m) + incoming_total;
			float delta = abs(pr[src] - old_pr);
			//if(delta == 0.0f) active[src] = false;
			local_diff += delta;
			//atomicAdd(diff, delta);
		}
	}
	float block_sum = BlockReduce(temp_storage).Sum(local_diff);
	if(threadIdx.x == 0) atomicAdd(diff, block_sum);
}
/*
__global__ void reduce(int m, float *cur_pagerank, float *next_pagerank, float *diff) {
	unsigned tid = blockIdx.x * blockDim.x + threadIdx.x;
	typedef cub::BlockReduce<float, BLKSIZE> BlockReduce;
	__shared__ typename BlockReduce::TempStorage temp_storage;
	int total_inputs = (m - 1) / (gridDim.x * blockDim.x) + 1;
	float local_diff = 0;
	for (int src = tid; total_inputs > 0; src += blockDim.x * gridDim.x, total_inputs--) {
		if (src < m) {
			float delta = abs(next_pagerank[src] - cur_pagerank[src]);
			local_diff += delta;
			//cur_pagerank[src] = next_pagerank[src];
		}
	}
	float block_sum = BlockReduce(temp_storage).Sum(local_diff);
	if(threadIdx.x == 0) atomicAdd(diff, block_sum);
}
//*/
void pr(int m, int nnz, int *d_row_offsets, int *d_column_indices, int *d_degree) {
	float *d_diff, h_diff;
	float *d_pr, *d_contrib;
	bool *d_active;
	double starttime, endtime, runtime;
	int iter = 0;
	const int nthreads = BLKSIZE;
	int nblocks = (m - 1) / nthreads + 1;
	CUDA_SAFE_CALL(cudaMalloc((void **)&d_pr, m * sizeof(float)));
	CUDA_SAFE_CALL(cudaMalloc((void **)&d_contrib, m * sizeof(float)));
	CUDA_SAFE_CALL(cudaMalloc((void **)&d_diff, sizeof(float)));
	//CUDA_SAFE_CALL(cudaMalloc((void **)&d_active, m * sizeof(bool)));
	initialize <<<nblocks, nthreads>>> (m, d_pr, d_active);
	CudaTest("initializing failed");

	size_t max_blocks = 5;
	max_blocks = maximum_residency(gather, nthreads, 0);
	printf("Solving, max_blocks=%d, nblocks=%d, nthreads=%d\n", max_blocks, nblocks, nthreads);
	starttime = rtclock();
	do {
		++iter;
		h_diff = 0.0f;
		CUDA_SAFE_CALL(cudaMemcpy(d_diff, &h_diff, sizeof(h_diff), cudaMemcpyHostToDevice));
		process<<<nblocks, nthreads>>>(m, d_pr, d_degree, d_contrib, d_active);
		CudaTest("solving kernel1 failed");
		gather<<<nblocks, nthreads>>>(m, d_row_offsets, d_column_indices, d_pr, d_contrib, d_diff, d_active);
		CudaTest("solving kernel2 failed");
		//reduce<<<nblocks, nthreads>>>(m, *d_pr_old, *d_pr_new, d_diff);
		//CudaTest("solving kernel3 failed");
		CUDA_SAFE_CALL(cudaMemcpy(&h_diff, d_diff, sizeof(h_diff), cudaMemcpyDeviceToHost));
		printf("iteration=%d, diff=%f\n", iter, h_diff);
	} while (h_diff > EPSILON && iter < MAX_ITER);
	CUDA_SAFE_CALL(cudaDeviceSynchronize());
	endtime = rtclock();
	printf("\titerations = %d.\n", iter);
	runtime = (1000.0f * (endtime - starttime));
	printf("\truntime [%s] = %f ms.\n", BFS_VARIANT, runtime);
	CUDA_SAFE_CALL(cudaFree(d_pr));
	CUDA_SAFE_CALL(cudaFree(d_contrib));
	CUDA_SAFE_CALL(cudaFree(d_diff));
	//CUDA_SAFE_CALL(cudaFree(d_active));
	return;
}
