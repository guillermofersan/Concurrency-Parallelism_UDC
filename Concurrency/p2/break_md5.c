#include <sys/types.h>
#include <openssl/md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define RED   "\x1b[31m"
#define RESET "\x1b[0m"
#define GREEN "\x1b[32m"

#define PASS_LEN 6
#define BAR_SIZE 50

#define NUM_THREADS 2
#define IT_THREAD 5



struct last_pass{
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    long val;
};


struct args{
    unsigned char **md5_num;
    char **dec_pass;
    int *found;
    struct last_pass *last_pass;
    int argnum;
};


struct thread_info {
    pthread_t    thread;
    struct args *args;
};

long ipow(long base, int exp)
{
    long res = 1;
    for (;;){
        if (exp & 1)
            res *= base;
        exp >>= 1;
        if (!exp)
            break;
        base *= base;
    }

    return res;
}

long pass_to_long(char *str) {
    long res = 0;

    for(int i=0; i < PASS_LEN; i++)
        res = res * 26 + str[i]-'a';

    return res;
}

void long_to_pass(long n, unsigned char *str) {  // str should have size PASS_SIZE+1
    for(int i=PASS_LEN-1; i >= 0; i--) {
        str[i] = n % 26 + 'a';
        n /= 26;
    }
    str[PASS_LEN] = '\0';
}

int hex_value(char c) {
    if (c>='0' && c <='9')
        return c - '0';
    else if (c>= 'A' && c <='F')
        return c-'A'+10;
    else if (c>= 'a' && c <='f')
        return c-'a'+10;
    else return 0;
}

void hex_to_num(char *str, unsigned char *hex) {
    for(int i=0; i < MD5_DIGEST_LENGTH; i++)
        hex[i] = (hex_value(str[i*2]) << 4) + hex_value(str[i*2 + 1]);
}

int allfound(int *found, int numargs){
    for (int i = 0; i < numargs; ++i) {
        if (found[i]==0){
            return 0;
        }
    }
    return 1;
}

void *break_pass(void *ptr) {
    struct args *args=ptr;
    struct last_pass *last_pass=args->last_pass;
    unsigned char res[MD5_DIGEST_LENGTH];
    unsigned char *pass = malloc((PASS_LEN + 1) * sizeof(char));
    long bound = ipow(26, PASS_LEN); // we have passwords of PASS_LEN
    // lowercase chars =>
    //     26 ^ PASS_LEN  different cases

    pthread_mutex_lock(&last_pass->mutex);
    while(last_pass->val<=bound && !allfound(args->found,args->argnum)){
        long count = last_pass->val;
        last_pass->val=count+IT_THREAD;
        pthread_mutex_unlock(&last_pass->mutex);

        for (int i=0; i < IT_THREAD; i++) {

            long_to_pass((count+i), pass);
            if((count+i)%1000000==0) {
                pthread_cond_broadcast(&last_pass->cond);
            }

            MD5(pass, PASS_LEN, res);
            for (int j = 0; j < args->argnum; ++j) {
                if(args->found[j] == 0 && 0 == memcmp(res, args->md5_num[j], MD5_DIGEST_LENGTH)){
                    args->found[j]=1;
                    strcpy(args->dec_pass[j],(char *)pass);
                    //printf("\r %s\n",pass);
                }
            }
        }

        pthread_mutex_lock(&last_pass->mutex);
    }
    pthread_mutex_unlock(&last_pass->mutex);

    free(pass);
    return NULL;
}

void npass_sec(void *lp){
    struct last_pass *last_pass = lp;
    long j;
    pthread_mutex_lock(&last_pass->mutex);
    j = last_pass->val;
    pthread_mutex_unlock(&last_pass->mutex);

    usleep(250000);
    pthread_mutex_lock(&last_pass->mutex);
    printf(" | %ld passwd/sec",(last_pass->val-j)*4);
    pthread_mutex_unlock(&last_pass->mutex);
}

_Noreturn void *loadingbar(void *ptr){

    struct last_pass *last_pass = ptr;
    long bound = ipow(26, PASS_LEN),i;

    int progress,per;


    for(;;){
        printf("\r");
        pthread_mutex_lock(&last_pass->mutex);
        pthread_cond_wait(&last_pass->cond,&last_pass->mutex);
        i=last_pass->val;
        progress = ((double)i / (double)bound)*BAR_SIZE;
        per = ((double)i / (double)bound)*100;


        printf(GREEN "[" RESET);
        for (int j = 0; j < BAR_SIZE; ++j) {
            if(j<progress)
                printf(GREEN "■" RESET); //█
            else printf(GREEN "_" RESET);
        }
        printf(GREEN "]" RESET " %2d%%",per);
        pthread_mutex_unlock(&last_pass->mutex);

        npass_sec(last_pass);


        fflush(stdout);
    }
}

void create_barthread(struct last_pass *last_pass){
    pthread_t barthread;

    //struct last_pass *lp = last_pass;

    if(pthread_create(&barthread,NULL,loadingbar,last_pass)!=0){
        printf(RED "Could not create thread" RESET);
        exit(1);
    }

}

struct last_pass *init_lastpass(){
    struct last_pass *lp = malloc(sizeof(struct last_pass));

    pthread_mutex_init(&lp->mutex, NULL);
    lp->val=0;
    pthread_cond_init(&lp->cond,NULL);

    return lp;
}
void initbar(){

    printf(GREEN "[");
    for (int j = 0; j < BAR_SIZE; ++j) {
        printf("_");
    }
    printf("]" RESET "0%%");
}

struct thread_info *start_threads(struct args *ptr)
{
    int i;
    struct thread_info *threads;
    struct args *args=ptr;

    threads = malloc(sizeof(struct thread_info) * NUM_THREADS);
    if (threads == NULL) {
        printf("Not enough memory\n");
        exit(1);
    }

    for (i = 0; i < NUM_THREADS; i++) {
        threads[i].args = args;

        if (0 != pthread_create(&threads[i].thread, NULL, break_pass, threads[i].args)) {
            printf("Could not create thread #%d", i);
            exit(1);
        }
    }

    return threads;
}

void wait(struct thread_info *threads) {
    // Wait for the threads to finish
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i].thread, NULL);

    free(threads);
}

unsigned char ** getmd5(int passnum, char*arguments[]){

    unsigned char ** md5_num = malloc(sizeof(unsigned char)*passnum*MD5_DIGEST_LENGTH);
    for (int i = 0; i < passnum; i++) {
        md5_num [i] = malloc(sizeof (unsigned char) * MD5_DIGEST_LENGTH);
        hex_to_num(arguments[i], md5_num[i]);
    }

    return md5_num;
}

struct args *init_args(struct last_pass *last_pass, char *arguments[], int argc){

    struct args *args = malloc(sizeof(struct args));
    int passnum = argc-1;

    args->md5_num = getmd5(argc-1,arguments+1);
    fflush(stdout);
    args->last_pass = last_pass;
    args->argnum=passnum;

    args->found = malloc(sizeof(int) * passnum);
    args->dec_pass = malloc(sizeof (char)*(PASS_LEN+1)*passnum);
    for (int i = 0; i < passnum; ++i) {
        args->dec_pass[i] = malloc(sizeof(char)*(PASS_LEN+1));
    }

    for (int i = 0; i < (argc-1); ++i) {
        args->found[i]=0;
    }

    return args;
}

void freeargs(struct args *args,int numpass){

    for (int i = 0; i < numpass; ++i) {
        free(args->md5_num[i]);
        free(args->dec_pass[i]);
    }
    free(args->dec_pass);
    free(args->found);
    free(args->md5_num);
    free(args);
}

int main(int argc, char *argv[]) {

    if(argc < 2) {
        printf("Use: %s string\n", argv[0]);
        exit(0);
    }

    struct thread_info *thrs;
    struct last_pass *last_pass = init_lastpass();

    printf("Breaking %d passwords using %d threads\n",argc-1,NUM_THREADS);

    initbar();
    struct args *args = init_args(last_pass,argv,argc);

    create_barthread(last_pass);
    thrs = start_threads(args);
    wait(thrs);

    printf("\n");

    for (int i = 0; i < argc-1; ++i) {
        if(args->found[i]==0){
            printf("%s: %d --> %s\n",argv[i+1], args->found[i], "not found!");
        } else
            printf("%s: %d --> %s\n",argv[i+1], args->found[i], args->dec_pass[i]);
    }

    freeargs(args,argc-1);
    free(last_pass);
    return 0;
}
