void SSSPVerifier(int m, int *row_offsets, int *column_indices, W_TYPE *weight, DistT *dist) {
	printf("Verifying...\n");
	int nerr = 0;
	for (int src = 0; src < m; src ++) {
		int row_begin = row_offsets[src];
		int row_end = row_offsets[src + 1]; 
		for (int offset = row_begin; offset < row_end; ++ offset) {
			int dst = column_indices[offset];
			DistT wt = weight? (DistT)weight[offset] : 1;
			if (wt > 0 && dist[src] + wt < dist[dst]) {
				//printf("%d %d %d %d\n", src, dst, dist[src], dist[dst]);
				++ nerr;
			}
		}
	}   
	printf("\tNumber of errors = %d.\n", nerr);
}

void write_solution(const char *fname, int m, DistT *h_dist) {
	assert(h_dist != NULL);
	printf("Writing solution to %s\n", fname);
	FILE *f = fopen(fname, "w");
	fprintf(f, "Computed solution (source dist): [");
	for(int node = 0; node < m; node++) {
		fprintf(f, "%d:%d\n ", node, h_dist[node]);
	}
	fprintf(f, "]");
}
