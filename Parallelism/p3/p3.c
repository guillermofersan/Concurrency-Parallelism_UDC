#include <stdio.h>
#include <sys/time.h>
#include <mpi.h>
#include <stdlib.h>

#define DEBUG 0

#define N 16

int round_up(int a, int b){
    if ((a%b)==0)
        return a/b;
    else
        return (a/b+1);
}

void print_matrix_v(int rows, int columns, float* vector){
    int i, j=0;

    for (i=0; i<rows; i++){
        for(int j = 0; j<columns; j++){
            printf("%.2f\t", vector[i*columns+j]);
        }
        printf("\n");
    }
}

int main(int argc, char *argv[] ) {

    int i, j;
    float matrix[N*N];
    float vector[N];
    float result[N];
    struct timeval tv1, tv2;
    int computation_time, communication_time = 0;

    MPI_Init(&argc, &argv);
    int numprocs, rank;
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    
    int block_size = round_up(N, numprocs), sendcounts[numprocs], recvcounts[numprocs], displs_send[numprocs], displs_recv[numprocs];

    /* Initialize Matrix and Vector, and the structures necessary for the collectives */
    if (rank == 0){
        for(i=0;i<N;i++) {
            vector[i] = i;
            for(j=0;j<N;j++) {
                matrix[i*N+j] = i+j;
            }
        }

        displs_send[0] = 0;
        displs_recv[0] = 0;
        sendcounts[0]  = block_size*N;
        recvcounts[0]  = block_size;
        
        for (i=1; i<numprocs; i++){
            displs_send[i] = displs_send[i-1] + sendcounts[i-1];
            displs_recv[i] = displs_recv[i-1] + block_size;
            recvcounts[i]  = block_size;
            sendcounts[i] = block_size*N;
        }
        recvcounts[numprocs-1] = N - block_size*(numprocs-1);
        sendcounts[i]    = N * (N - block_size*(numprocs-1));

        printf("Original matrix:\n");
        print_matrix_v(N, N, (float*)matrix);
    }
    
    float* recbuf;
    int thisblock = block_size;
    if (rank == numprocs-1){
        thisblock = N - block_size*(numprocs-1);
    }
    
    recbuf = malloc(N*block_size*sizeof(float));

    gettimeofday(&tv1, NULL);
    MPI_Bcast(vector, N, MPI_FLOAT, 0, MPI_COMM_WORLD);
    MPI_Scatterv(matrix, sendcounts, displs_send, MPI_FLOAT, recbuf, thisblock*N, MPI_FLOAT, 0, MPI_COMM_WORLD);
    gettimeofday(&tv2, NULL);

    communication_time = (tv2.tv_usec-tv1.tv_usec) + 1000000*(tv2.tv_sec - tv1.tv_sec);

    float preliminar_result[thisblock];

    gettimeofday(&tv1, NULL);
    
    for(i=0;i<thisblock;i++) { 
        preliminar_result[i]=0;
        for(j=0;j<N;j++) {
            preliminar_result[i] += recbuf[i*N+j]*vector[j];
        }
    }

    gettimeofday(&tv2, NULL);
    computation_time = (tv2.tv_usec-tv1.tv_usec) + 1000000*(tv2.tv_sec - tv1.tv_sec);
    

    gettimeofday(&tv1, NULL);

    MPI_Gatherv(preliminar_result, thisblock, MPI_FLOAT, result, recvcounts, displs_recv, MPI_FLOAT, 0, MPI_COMM_WORLD);

    gettimeofday(&tv2, NULL);    
    communication_time += (tv2.tv_usec-tv1.tv_usec) + 1000000*(tv2.tv_sec - tv1.tv_sec);


    if (DEBUG && rank==0){
        printf("\nResult matrix:\n");
        print_matrix_v(N,1,result);
    }
    else if (!DEBUG){
        for (i = 0; i<numprocs; i++){
            MPI_Barrier(MPI_COMM_WORLD);
            if (rank==i){
                printf("\nProcess %d time info:\n", rank);
                printf("Communication time = %d ms\n", communication_time);
                printf("Computation time = %d ms\n", computation_time);
            }
        }
    }

    free(recbuf);
    MPI_Finalize();
    return 0;
}

