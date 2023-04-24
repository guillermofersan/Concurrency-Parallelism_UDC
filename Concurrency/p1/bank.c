#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "options.h"

#define MAX_AMOUNT 20

struct bank {
    int num_accounts;        // number of accounts
    int *accounts;           // balance array
    pthread_mutex_t *mutex;  // mutex array
};

struct iter {
    int iterations;         //
    pthread_mutex_t mutex;  // mutex
};

struct args {
    int          thread_num;  // application defined thread #
    int          delay;       // delay between operations
    int	         iterations;  // number of operations
    int          net_total;   // total amount deposited by this thread
    struct bank *bank;        // pointer to the bank (shared with other threads)
    struct iter *iter;
};

struct thread_info {
    pthread_t    id;    // id returned by pthread_create()
    struct args *args;  // pointer to the arguments
};

// Threads run on this function
void *deposit(void *ptr)
{
    struct args *args =  ptr;
    int amount, account, balance;

    pthread_mutex_lock(&args->iter->mutex);
    while(args->iter->iterations) {
        args->iter->iterations--;
        pthread_mutex_unlock(&args->iter->mutex);
        amount  = rand() % MAX_AMOUNT;
        account = rand() % args->bank->num_accounts;

        pthread_mutex_lock(&args->bank->mutex[account]);

        printf("Thread %d depositing %d on account %d\n",
               args->thread_num, amount, account);

        balance = args->bank->accounts[account];
        if(args->delay) usleep(args->delay); // Force a context switch

        balance += amount;
        if(args->delay) usleep(args->delay);

        args->bank->accounts[account] = balance;
        if(args->delay) usleep(args->delay);

        args->net_total += amount;
        pthread_mutex_unlock(&args->bank->mutex[account]);
        pthread_mutex_lock(&args->iter->mutex);
    }
    pthread_mutex_unlock(&args->iter->mutex);
    return NULL;
}

void *transfer(void *ptr)
{
    struct args *args =  ptr;
    int amount, origin, destination, balance;

    pthread_mutex_lock(&args->iter->mutex);
    while(args->iter->iterations) {
        args->iter->iterations--;
        pthread_mutex_unlock(&args->iter->mutex);
        origin  = rand() % args->bank->num_accounts;
        do{
            destination = rand() % args->bank->num_accounts;
        } while (origin==destination);

        if (origin<destination){
            pthread_mutex_lock(&args->bank->mutex[origin]);
            pthread_mutex_lock(&args->bank->mutex[destination]);
        } else {
            pthread_mutex_lock(&args->bank->mutex[destination]);
            pthread_mutex_lock(&args->bank->mutex[origin]);
        }

        amount  = rand() % (args->bank->accounts[origin]+1);

        printf("Thread %d transfering %d from account %d to account %d\n",
               args->thread_num, amount, origin, destination);

        balance = args->bank->accounts[origin];
        if(args->delay) usleep(args->delay); // Force a context switch

        balance -= amount;
        if(args->delay) usleep(args->delay);

        args->bank->accounts[origin] = balance;
        if(args->delay) usleep(args->delay);

        balance = args->bank->accounts[destination];
        if(args->delay) usleep(args->delay);

        balance += amount;
        if(args->delay) usleep(args->delay);

        args->bank->accounts[destination] = balance;
        if(args->delay) usleep(args->delay);

        args->net_total += amount;

        pthread_mutex_unlock(&args->bank->mutex[origin]);
        pthread_mutex_unlock(&args->bank->mutex[destination]);

        pthread_mutex_lock(&args->iter->mutex);
    }
    pthread_mutex_unlock(&args->iter->mutex);

    return NULL;
}


void *calctotal(void *ptr){

    struct args *args =  ptr;
    int amount;

    while(1){
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL);
        amount=0;
        for (int i = 0; i < args->bank->num_accounts; ++i) {
            pthread_mutex_lock(&args->bank->mutex[i]);
        }

        for (int i = 0; i < args->bank->num_accounts; ++i) {
            amount+= args->bank->accounts[i];
        }

        printf("\x1b[31m" "Total: %d\n" "\x1b[0m",amount);


        for (int i = 0; i < args->bank->num_accounts; ++i) {
            pthread_mutex_unlock(&args->bank->mutex[i]);
        }
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
        pthread_testcancel();
        if(args->delay) usleep(10);

    }
}

// start opt.num_threads threads running on deposit.
struct thread_info *start_threads(struct options opt, struct bank *bank, struct iter *iter, void* func)
{
    int i;
    struct thread_info *threads;

    printf("creating %d threads\n", opt.num_threads);
    threads = malloc(sizeof(struct thread_info) * opt.num_threads);

    if (threads == NULL) {
        printf("Not enough memory\n");
        exit(1);
    }

    // Create num_thread threads running swap()
    for (i = 0; i < opt.num_threads; i++) {
        threads[i].args = malloc(sizeof(struct args));

        threads[i].args -> thread_num = i;
        threads[i].args -> net_total  = 0;
        threads[i].args -> bank       = bank;
        threads[i].args -> iter       = iter;
        threads[i].args -> delay      = opt.delay;
        threads[i].args -> iterations = opt.iterations;

        if (0 != pthread_create(&threads[i].id, NULL, func, threads[i].args)) {
            printf("Could not create thread #%d", i);
            exit(1);
        }
    }

    return threads;
}

struct thread_info *start_totalthread(struct options opt, struct bank *bank)
{
    struct thread_info *thread;

    printf("creating total thread\n");
    thread = malloc(sizeof(struct thread_info));

    if (thread == NULL) {
        printf("Not enough memory\n");
        exit(1);
    }

    // Create thread running swap()
    thread->args = malloc(sizeof(struct args));
    thread->args -> thread_num = 0;
    thread->args -> net_total  = 0;
    thread->args -> bank       = bank;
    thread->args -> delay      = 100;
    thread->args -> iterations = opt.num_threads;
    if (0 != pthread_create(&thread->id, NULL, calctotal, thread->args)) {
        printf("Could not create thread");
        exit(1);
    }


    return thread;
}

void print_netdeposit(struct bank *bank, struct thread_info *thrs, int num_threads) {
    int total_deposits=0;
    printf("\nNet deposits by thread\n");

    for(int i=0; i < num_threads; i++) {
        printf("%d: %d\n", i, thrs[i].args->net_total);
        total_deposits += thrs[i].args->net_total;
    }
    printf("Total: %d\n", total_deposits);
}

// Print the final balances of accounts and threads
void print_balance(struct bank *bank) {
    int bank_total=0;

    printf("\nAccount balance\n");
    for(int i=0; i < bank->num_accounts; i++) {
        printf("%d: %d\n", i, bank->accounts[i]);
        bank_total += bank->accounts[i];
    }
    printf("Total: %d\n", bank_total);
}

// wait for all threads to finish, print totals, and free memory
void wait(struct options opt, struct bank *bank, struct thread_info *threads) {
    // Wait for the threads to finish
    for (int i = 0; i < opt.num_threads; i++)
        pthread_join(threads[i].id, NULL);

    print_netdeposit(bank, threads, opt.num_threads);
    print_balance(bank);
    for (int i = 0; i < opt.num_threads; i++)
        free(threads[i].args);

    free(threads);
}
void waittransfers(struct options opt, struct bank *bank, struct thread_info *threads, struct thread_info *totalthread) {
    // Wait for the threads to finish
    for (int i = 0; i < opt.num_threads; i++)
        pthread_join(threads[i].id, NULL);


    pthread_cancel(totalthread->id);
    pthread_join(totalthread->id,NULL);

    print_balance(bank);
    for (int i = 0; i < opt.num_threads; i++)
        free(threads[i].args);



    free(threads);
    free(totalthread);
}

struct iter *init_iterator(int iterations){
    struct iter *it= malloc(sizeof(struct iter));
    it->iterations=iterations;
    pthread_mutex_init(&it->mutex,NULL);

    return it;
}

// allocate memory, and set all accounts to 0
void init(struct bank *bank, int num_accounts) {
    bank->num_accounts = num_accounts;
    bank->accounts     = malloc(bank->num_accounts * sizeof(int));
    bank->mutex        = malloc(bank->num_accounts * sizeof(pthread_mutex_t));

    for(int i=0; i < bank->num_accounts; i++){
        bank->accounts[i] = 0;
        pthread_mutex_init(&bank->mutex[i],NULL);
    }

}

int main (int argc, char **argv)
{

    struct options      opt;
    struct bank         bank;
    struct thread_info *thrs;
    struct thread_info *totalthread;

    srand(time(NULL));


    // Default values for the options
    opt.num_threads  = 5;
    opt.num_accounts = 10;
    opt.iterations   = 100;
    opt.delay        = 10;

    read_options(argc, argv, &opt);

    struct iter *iter = init_iterator(opt.iterations);
    init(&bank, opt.num_accounts);

    thrs = start_threads(opt, &bank, iter, deposit);
    wait(opt, &bank, thrs);
    printf("\n");
    iter->iterations=opt.iterations;
    totalthread = start_totalthread(opt,&bank);
    thrs = start_threads(opt, &bank, iter, transfer);

    waittransfers(opt, &bank, thrs,totalthread);


    free(bank.accounts);
    return 0;
}
