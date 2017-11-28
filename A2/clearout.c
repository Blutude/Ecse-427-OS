#define _SVID_SOURCE
#define _BSD_SOURCE
#define _XOPEN_SOURCE 500
#define _XOPEN_SORUCE 600
#define _XOPEN_SORUCE 600

#include <semaphore.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define BUFF_SHM "/OS_BUFF"
#define BUFF_MUTEX_A "/OS_MUTEX_A"
#define BUFF_MUTEX_B "/OS_MUTEX_B"

int main()
{
    printf("starting to clear namespaces\n");
    //clearing buffer namespace
    
    //unlink shm
    shm_unlink(BUFF_SHM);
   
    //unlink semaphore
    sem_unlink(BUFF_MUTEX_A);
    sem_unlink(BUFF_MUTEX_B);
  
    printf("cleared namespaces\n");
    return 0;
}
