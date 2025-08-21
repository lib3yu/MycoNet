#ifndef MYCONET_PORT_H
#define MYCONET_PORT_H

#include <myconet_conf.h>

#include <pthread.h>
#define Mem_alloc(size)   malloc(size)
#define Mem_free(ptr)     free(ptr)
#if MN_CONF_USE_LOCK
#define Mutex_t           pthread_mutex_t
#define Mutex_init(m)     pthread_mutex_init(m, NULL)
#define Mutex_lock(m)     pthread_mutex_lock(m)
#define Mutex_unlock(m)   pthread_mutex_unlock(m)
#define Mutex_destroy(m)  pthread_mutex_destroy(m)

#if 1
#define Rwlock_t            pthread_rwlock_t
#define Rwlock_init(m)      pthread_rwlock_init(m, NULL)
#define Rwlock_rdlock(m)    pthread_rwlock_rdlock(m)
#define Rwlock_rdunlock(m)  pthread_rwlock_unlock(m)
#define Rwlock_wrlock(m)    pthread_rwlock_wrlock(m)
#define Rwlock_wrunlock(m)  pthread_rwlock_unlock(m)
#define Rwlock_destroy(m)   pthread_rwlock_destroy(m)
#else
#define Rwlock_t            pthread_mutex_t
#define Rwlock_init(m)      pthread_mutex_init(m, NULL)
#define Rwlock_rdlock(m)    pthread_mutex_lock(m)
#define Rwlock_rdunlock(m)  pthread_mutex_unlock(m)
#define Rwlock_wrlock(m)    pthread_mutex_lock(m)
#define Rwlock_wrunlock(m)  pthread_mutex_unlock(m)
#define Rwlock_destroy(m)   pthread_mutex_destroy(m)
#endif

#else /* MN_CONF_USE_LOCK */

#define Mutex_t           
#define Mutex_init(m)     
#define Mutex_lock(m)     
#define Mutex_unlock(m)   
#define Mutex_destroy(m)  

#define Rwlock_t            
#define Rwlock_init(m)      
#define Rwlock_rdlock(m)    
#define Rwlock_rdunlock(m)  
#define Rwlock_wrlock(m)    
#define Rwlock_wrunlock(m)  
#define Rwlock_destroy(m)   

#endif /* MN_CONF_USE_LOCK */





#endif 
