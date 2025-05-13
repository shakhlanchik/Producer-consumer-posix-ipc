#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <stddef.h>

#define data_max (((256 + 3) / 4) * 4)
#define mes_max 10
#define max_child 1024

typedef struct{
    int type;
    int hash;
    int size;
    char data[data_max];
}mes;

typedef struct{
    mes buf[mes_max];
    int head;
    int tail;
    int counter;
    int injected;
    int extracted;
}message_queue;

message_queue* queue;
sem_t *mutex;
sem_t *free_space;
sem_t *items;

pid_t  prods[max_child];
int prod_num;

pid_t  cons[max_child];
int cons_num;

static pid_t pid;

void init_queue(){
    queue->extracted = 0;
    queue->injected = 0;
    queue->counter = 0;
    queue->head = 0;
    queue->tail = 0;
    memset(queue->buf, 0, sizeof(queue->buf));
}

void check_sems(){
    pid = getpid();
    int fd = shm_open("/queue", (O_RDWR | O_CREAT | O_TRUNC),
                      (S_IRUSR | S_IWUSR));
    if(fd < 0){
        fprintf(stderr, "shm_open");
        exit(1);
    }
    if(ftruncate(fd, sizeof(message_queue))){
        fprintf(stderr, "ftruncate");
        exit(1);
    }
    void* ptr = mmap(NULL, sizeof(message_queue),
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(ptr == MAP_FAILED){
        fprintf(stderr, "mmap");
        exit(1);
    }
    queue = (message_queue *) ptr;
    if(close(fd)){
        fprintf(stderr, "close");
        exit(1);
    }
    init_queue();
    if((mutex = sem_open("mutex", (O_RDWR | O_CREAT | O_TRUNC), (S_IRUSR | S_IWUSR), 1)) == SEM_FAILED
    || (free_space = sem_open("free_space", (O_RDWR | O_CREAT | O_TRUNC), (S_IRUSR | S_IWUSR),  mes_max)) == SEM_FAILED
    || (items = sem_open("items", (O_RDWR | O_CREAT | O_TRUNC), (S_IRUSR | S_IWUSR), 0)) == SEM_FAILED){
        fprintf(stderr, "sem_open");
        exit(1);
    }
}

int hash(mes* msg)
{
    unsigned long hash = 5381;
    for (int i = 0; i < msg->size + 4; i++)
        hash = ((hash << 5) + hash) + i;
    return (int) hash;
}

void prod_mes(mes* msg){
    int val = rand() % 257;
    if (val == 256)
        msg->type = -1;
    else
    {
        msg->type = 0;
        msg->size = val;
    }
    for (int i = 0; i < val; ++i)
        msg->data[i] = (char) (rand() % 256);
    msg->hash = 0;
    msg->hash = hash(msg);
}

void consume_mes(mes* msg){
    int mes_hash = msg->hash;
    msg->hash = 0;
    int check = hash(msg);
    if(check != mes_hash){
        fprintf(stderr, "Check sum (= %d) not equal msg_hash (= %d)\n",check, mes_hash);
    }
    msg->hash = mes_hash;
}

int put_msg(mes* msg){
    if(queue->counter == mes_max){
        fprintf(stdout, "Buf is full!\n");
        exit(1);
    }
    if(queue->tail == mes_max)
        queue->tail = 0;
    queue->buf[queue->tail] = *msg;
    queue->tail++;
    queue->counter++;
    return ++queue->injected;
}

int get_msg(mes* msg){
    if(queue->counter == 0){
        fprintf(stderr, "There are no messages in queue\n");
        exit(1);
    }
    if(queue->head == mes_max)
        queue->head = 0;
    queue->buf[queue->head] = *msg;
    queue->head++;
    queue->counter--;
    return ++queue->extracted;
}

void create_prod() {
    if(prod_num == max_child - 1){
        fprintf(stderr, "Max count of prods");
        exit(1);
    }
    switch(prods[prod_num] = fork()) {
        default: {
            prod_num++;
            return;
        }
        case 0: {
            srand(getpid());
            break;
        }
        case -1: {
            fprintf(stderr, "fork");
            exit(1);
        }
    }
        mes msg;
    int counter, temp;
    while(true){
        sem_getvalue(free_space, &temp);
        if(!temp){
            fprintf(stderr, "Queue is full, prod with pid = %d is waiting\n", getpid());
        }
            prod_mes(&msg);
            sem_wait(free_space);
            sem_wait(mutex);
            counter = put_msg(&msg);
            sem_post(mutex);
            sem_post(items);
            fprintf(stdout,"Pid = %d\nMessage = %X\nMessages injected counter = %d\n", getpid(), msg.hash, counter);
            sleep(8);
        }
}

void create_cons() {
    if(prod_num == max_child - 1){
        fprintf(stderr, "Max count of prods");
        exit(1);
    }
    switch(cons[cons_num] = fork()) {
        default: {
            cons_num++;
            return;
        }
        case 0: {
            break;
        }
        case -1: {
            fprintf(stderr, "fork");
            exit(1);
        }
    }
    mes msg;
    int counter, temp;
    while(true){
        sem_getvalue(free_space, &temp);
        if(temp){
            fprintf(stderr, "Queue is empty, cons with pid = %d is waiting\n", getpid());
        }
        sem_wait(items);
        sem_wait(mutex);
        counter = get_msg(&msg);
        sem_post(mutex);
        sem_post(free_space);
        consume_mes(&msg);
        fprintf(stdout,"Pid = %d\nMessage = %X\nMessages extracted counter = %d\n", getpid(), msg.hash, counter);
        sleep(8);
    }
}
void del_prod(){
    if(prod_num == 0){
        fprintf(stderr, "No prods to delete");
        return;
    }
    prod_num--;
    kill(prods[prod_num], SIGKILL);
    wait(NULL);
}

void del_cons(){
    if(cons_num == 0){
        fprintf(stderr, "No cons to delete");
        return;
    }
    cons_num--;
    kill(cons[cons_num], SIGKILL);
    wait(NULL);
}

int main(){
    check_sems();
    fprintf(stdout, "p - create producer\n");
    fprintf(stdout, "c - create consumer\n");
    fprintf(stdout, "d - delete producer\n");
    fprintf(stdout, "r - delete consumer\n");
    fprintf(stdout, "l - show processes\n");
    fprintf(stdout, "q - quit program\n");
    while(true){
        switch(getchar()){
            case 'p' : { create_prod(); break; }
            case 'c' : { create_cons(); break; }
            case 'd' : { del_prod(); break; }
            case 'r' : { del_cons(); break; }
            case 'l' : {
                for (int i = 0; i < prod_num; i++)
                    fprintf(stdout,"Producer %d: %d\n", i + 1, prods[i]);
                fprintf(stdout,"\n");
                for (int i = 0; i < cons_num; i++)
                    printf("Consumer %d: %d\n", i + 1, cons[i]);
                fprintf(stdout,"\n");
                break; }
            case 'q' : {
                if (getpid() == pid)
                {
                    for (int i = 0; i < prod_num; ++i)
                    {
                        kill(prods[i], SIGKILL);
                        wait(NULL);
                    }
                    for (int i = 0; i < cons_num; ++i)
                    {
                        kill(cons[i], SIGKILL);
                        wait(NULL);
                    }
                }
                else if (getppid() == pid)
                    kill(getppid(), SIGKILL);
                if (shm_unlink("/queue"))
                {
                    fprintf(stderr, "shm_unlink");
                    abort();
                }
                if (sem_unlink("mutex") || sem_unlink("free_space") || sem_unlink("items"))
                {
                    fprintf(stderr, "sem_unlink");
                    abort();
                }
               return 0;
            }
        }
    }

}