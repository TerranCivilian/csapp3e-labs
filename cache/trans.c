/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 
#include <stdio.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);
void trans32(int A[32][32], int B[32][32]);
void trans64(int A[64][64], int B[64][64]);
void trans61x67(int A[67][61], int B[61][67]);
void block4x4(int i, int j, int A[64][64], int B[64][64]);

/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. 
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
	if (M==32 && N==32) {
		trans32(A, B);
	} else if (M==64 && N==64) {
		trans64(A, B);
	} else if (M==61 && N==67) {
		trans61x67(A, B);
	}
}

// Called from the submission function for 32x32 matrices.
void trans32(int A[32][32], int B[32][32])
{
	int i, j, ii, jj, diag = 0, d_index;

	for (i = 0; i < 32; i += 8) {
		for (j = 0; j < 32; j += 8) {
			for (ii = i; ii < i+8; ++ii) {
				for (jj = j; jj < j+8; ++jj) {
					if (ii == jj) {
						diag = 1;
						d_index = ii;
					} else {
						B[jj][ii] = A[ii][jj];
					}
				}
				if (diag) {
					B[d_index][d_index] = A[d_index][d_index];
					diag = 0;
				}
			}
		}
	}
}

// Called from the submission function for 64x64 matrices.
// My method results in 1,427 misses so it doesn't earn full points (<1,300), but it's the
// best I've got so far.
void trans64(int A[64][64], int B[64][64])
{
	int i, j;

	for (i = 0; i < 64; i += 8) {
		for (j = 0; j < 64; j += 8) {
			block4x4(i+0, j+0, A, B);
			block4x4(i+4, j+0, A, B);
			block4x4(i+4, j+4, A, B);
			block4x4(i+0, j+4, A, B);
		}
	}
}

// Called from the submission function for (M,N) = (61,67)
void trans61x67(int A[67][61], int B[61][67])
{
	int i, j, ii, jj;

	for (i = 0; i < 67; i += 16) {
		for (j = 0; j < 61; j += 16) {
			for (ii = i; (ii < i+16) && (ii < 67); ++ii) {
				for (jj = j; (jj < j+16) && (jj < 61); ++jj) {
					B[jj][ii] = A[ii][jj];
				}
			}
		}
	}
}

// Performs matrix transpose on a the 4x4 block whose upper left-most element is A[i][j]
// For each row, the block's diagonal element is assigned last to avoid potential cache
// miss
void block4x4(int i, int j, int A[64][64], int B[64][64])
{
	int ii, jj;

	for (ii = 0; ii < 4; ++ii) {
		for (jj = 0; jj < 4; ++jj) {
			if (ii == jj) {
				// perform this assignment after the other 3
			} else {
				B[j+jj][i+ii] = A[i+ii][j+jj];
			}
		}
		// each block's diagonal case; only necessary for certain blocks along the
		// diagonal of the whole 64x64 array, but it simplifies the code to just do
		// them all this way. i'm pretty sure it won't result in unnecessary misses
		B[j+ii][i+ii] = A[i+ii][j+ii];
	}
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 
}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}

