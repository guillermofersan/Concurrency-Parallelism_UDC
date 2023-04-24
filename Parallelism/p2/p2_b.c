#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <mpi.h>

int ipow(int base, int exp){
    if (exp == 0) return 1;
    int res=base;
    for (int i=exp; i>1; i--){
        res *= base;
    }
    return res;
}

int MPI_BinomialBcast(void* buff, int count, MPI_Datatype datatype, int root, MPI_Comm comm){
    int error, myrank, numprocs, send_to;
    
    error = MPI_SUCCESS;
    MPI_Comm_size(comm, &numprocs);
    MPI_Comm_rank(comm, &myrank);
    
    if (myrank != 0) {  //All processes except the root (assumed 0) must receive
        error = MPI_Recv(buff, count, datatype, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, MPI_STATUS_IGNORE);
        if (error != MPI_SUCCESS) return error;
    }

    for (int i = 1; (send_to=myrank+ipow(2,i-1)) <= numprocs-1; i++){
        if (myrank<ipow(2,i-1)){
            //printf("I am process %d and I send to %d\n", myrank, send_to);
            error = MPI_Send(buff, count, datatype, send_to, 0, comm);
            if (error!=MPI_SUCCESS) return error;
        }
    }
    
    return error;
}

int MPI_FlattreeColective(void* buff, void* recbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm){
    int numprocs, rank;
    
    int error = MPI_SUCCESS;
    MPI_Comm_size(comm, &numprocs);
    MPI_Comm_rank(comm, &rank);

    int indiv_recv;

    if (rank == root){
        int* addition = recbuf; 
        *addition = *(int*) buff;
        for (int i = 1; i<numprocs; i++){
            error = MPI_Recv(&indiv_recv, count, datatype, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, MPI_STATUS_IGNORE);
            *addition += (int) indiv_recv;
        }
    }
    else
        error = MPI_Send(buff, count, datatype, root, 0, comm);
    
    return error;
}

int main(int argc, char *argv[])
{
    int i, done = 0, n, count;
    double PI25DT = 3.141592653589793238462643;
    double pi, x, y, z;
    MPI_Init(&argc, &argv);
    int numprocs, rank;
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    while (!done)
    {
        if (rank==0){
            printf("Enter the number of points: (0 quits) \n");
            scanf("%d",&n);
        }
        MPI_BinomialBcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);

        if (n == 0)
            break;

        count = 0;  

        for (i = rank; i < n; i+=numprocs) {
            // Get the random numbers between 0 and 1
            x = ((double) rand()) / ((double) RAND_MAX);
            y = ((double) rand()) / ((double) RAND_MAX);

            // Calculate the square root of the squares
            z = sqrt((x*x)+(y*y));

            // Check whether z is within the circle
            if(z <= 1.0)
                count++;
        }
        int received_count = count;
            MPI_FlattreeColective(&count, &received_count, 1, MPI_INT,MPI_SUM, 0, MPI_COMM_WORLD);
        if(rank == 0){
            pi = ((double) received_count/(double) n)*4.0;
            printf("pi is approx. %.16f, Error is %.16f\n", pi, fabs(pi - PI25DT));
        }

    }
    MPI_Finalize();
}
