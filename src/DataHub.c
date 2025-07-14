#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "DataHub.h"

//==============================================================================
// Platform Abstraction Layer (PAL) for OS primitives
//==============================================================================

// Define USE_FREERTOS in your project/makefile to enable this section
#if defined(USE_FREERTOS)
    #include "FreeRTOS.h"
    #include "task.h"
    #include "semphr.h"

    #define Mem_alloc(size) pvPortMalloc(size)
    #define Mem_free(ptr)   vPortFree(ptr)

    // --- FreeRTOS Mutex Wrapper ---
    #define Mutex_t         SemaphoreHandle_t
    #define Mutex_init(m)   (*(m) = xSemaphoreCreateMutex())
    #define Mutex_lock(m)   xSemaphoreTake(*(m), portMAX_DELAY)
    #define Mutex_unlock(m) xSemaphoreGive(*(m))
    #define Mutex_destroy(m) vSemaphoreDelete(*(m))

    // --- FreeRTOS Read-Write Lock Implementation ---
    // Implemented using a mutex for write lock, a semaphore for writer preference,
    // and a counter for readers.
    typedef struct {
        SemaphoreHandle_t write_mutex;    // Exclusive write lock
        SemaphoreHandle_t read_sem;       // Lock for read_count manipulation
        volatile int      read_count;     // Number of active readers
    } Rwlock_t;

    static int Rwlock_init(Rwlock_t *rwlock) {
        if (!rwlock) return -1;
        rwlock->write_mutex = xSemaphoreCreateMutex();
        if (rwlock->write_mutex == NULL) return -1;
        rwlock->read_sem = xSemaphoreCreateMutex();
        if (rwlock->read_sem == NULL) {
            vSemaphoreDelete(rwlock->write_mutex);
            return -1;
        }
        rwlock->read_count = 0;
        return 0;
    }

    static void Rwlock_destroy(Rwlock_t *rwlock) {
        if (!rwlock) return;
        vSemaphoreDelete(rwlock->write_mutex);
        vSemaphoreDelete(rwlock->read_sem);
    }

    static void Rwlock_rdlock(Rwlock_t *rwlock) {
        xSemaphoreTake(rwlock->read_sem, portMAX_DELAY);
        rwlock->read_count++;
        if (rwlock->read_count == 1) {
            // First reader locks out writers
            xSemaphoreTake(rwlock->write_mutex, portMAX_DELAY);
        }
        xSemaphoreGive(rwlock->read_sem);
    }

    static void Rwlock_wrlock(Rwlock_t *rwlock) {
        xSemaphoreTake(rwlock->write_mutex, portMAX_DELAY);
    }

    static void Rwlock_unlock(Rwlock_t *rwlock) {
        // This unlock is for both read and write locks.
        // A writer just gives the mutex. A reader must check the count.
        if (xSemaphoreGetMutexHolder(rwlock->write_mutex) == xTaskGetCurrentTaskHandle() && uxSemaphoreGetCount(rwlock->write_mutex) == 0) {
            // This task holds the write lock
            xSemaphoreGive(rwlock->write_mutex);
        } else {
            // This task holds a read lock
            xSemaphoreTake(rwlock->read_sem, portMAX_DELAY);
            rwlock->read_count--;
            if (rwlock->read_count == 0) {
                // Last reader unlocks for writers
                xSemaphoreGive(rwlock->write_mutex);
            }
            xSemaphoreGive(rwlock->read_sem);
        }
    }

#elif defined(__GNUC__) && !defined(USE_FREERTOS) // POSIX (Linux, macOS)
    #include <pthread.h>
    #define Mem_alloc(size)   malloc(size)
    #define Mem_free(ptr)     free(ptr)

    #define Mutex_t           pthread_mutex_t
    #define Mutex_init(m)     pthread_mutex_init(m, NULL)
    #define Mutex_lock(m)     pthread_mutex_lock(m)
    #define Mutex_unlock(m)   pthread_mutex_unlock(m)
    #define Mutex_destroy(m)  pthread_mutex_destroy(m)

    // #define Rwlock_t          pthread_rwlock_t
    // #define Rwlock_init(m)    pthread_rwlock_init(m, NULL)
    // #define Rwlock_rdlock(m)  pthread_rwlock_rdlock(m)
    // #define Rwlock_wrlock(m)  pthread_rwlock_wrlock(m)
    // #define Rwlock_unlock(m)  pthread_rwlock_unlock(m)
    // #define Rwlock_destroy(m) pthread_rwlock_destroy(m)
    #define Rwlock_t          pthread_mutex_t
    #define Rwlock_init(m)    pthread_mutex_init(m, NULL)
    #define Rwlock_rdlock(m)  pthread_mutex_lock(m)
    #define Rwlock_wrlock(m)  pthread_mutex_lock(m)
    #define Rwlock_unlock(m)  pthread_mutex_unlock(m)
    #define Rwlock_destroy(m) pthread_mutex_destroy(m)

    #define Cond_t           pthread_cond_t
    #define Cond_init(c)     pthread_cond_init(c, NULL)
    #define Cond_wait(c, m)  pthread_cond_wait(c, m)
    #define Cond_signal(c)   pthread_cond_signal(c)
    #define Cond_broadcast(c) pthread_cond_broadcast(c)
    #define Cond_destroy(c)  pthread_cond_destroy(c)

#else
    #error "Unsupported platform or compiler. Define USE_FREERTOS or ensure you are on a POSIX system with GCC/Clang."
#endif

typedef \
struct ll_node {
    struct ll_node *next;
    DataNode_t     *data;
} ll_node_t;

typedef \
struct ll_list {
    ll_node_t *head;
    ll_node_t *tail;
    size_t     size;
} ll_list_t;

typedef \
struct mem_queue {
    void *buff;
    uint32_t capacity;
    uint32_t item_size;

    struct {
        uint32_t head;
        uint32_t tail;
        uint32_t count;
        
        Mutex_t lock;
        Cond_t not_full;
        Cond_t not_empty;

        uint32_t max_used;
    } priv;
} mem_queue_t;

typedef \
__attribute__((aligned(4)))
struct AsyncEvent {
    DataNode_t *node_p;
    EventCode_t event;
} AsyncEvent_t;

struct DataNodePriv {
    atomic_bool  is_inited;
    atomic_bool  is_registered;
    ll_list_t    subscribers;
    Mutex_t      subscribers_lock;
    ll_list_t    subscriptions;
    Mutex_t      subscriptions_lock;
    void*        cache_p;
    Rwlock_t     cache_lock;
#if DH_USE_STATIC_NODE_LIST
    ll_node_t    static_node_self;
#endif
};

typedef \
struct DataHub {
    const char*   name;
    ll_list_t     node_list;
    Rwlock_t      list_lock;
    atomic_bool   is_inited;
    mem_queue_t async_pub_queue;
} DataHub_t;

#if ASYNC_EVENT_QUEUE_STATIC
static __attribute__((aligned(4))) \
    uint8_t s_queue_buffer[sizeof(AsyncEvent_t) * ASYNC_EVENT_QUEUE_SIZE] = {0};
#endif

static DataHub_t s_hub = {
    .name = "GlobalDataHub",
    .is_inited = ATOMIC_VAR_INIT(false),
};

// Static assertion to ensure private data fits into the reserved space
_Static_assert(sizeof(struct DataNodePriv) <= DATAHUB_PRIV_DATA_SIZE,
               "DATAHUB_PRIV_DATA_SIZE is too small for internal private data!");


#define node_priv(node_p) ((struct DataNodePriv *)((node_p)->priv))
#define hub_p() (&s_hub)

//==============================================================================
// Dummy Node Definition
//==============================================================================

static DataNode_t s_dummyNode = {
    .name = "_DummyNode_",
    .conflags = CONF_NONE,
    .event_msk = EVENT_NONE,
    .event_cb = NULL, // No event callback
    .user_data = NULL, // No user data
    .size = 0,
};

DataNode_t * const _dummyNode = &s_dummyNode;

//==============================================================================
// Internal lock-free queue Implementation
//==============================================================================

static int mem_queue_create(mem_queue_t *q, uint32_t capacity, uint32_t item_size);
static int mem_queue_create_static(mem_queue_t *q, void *buffer, uint32_t capacity, uint32_t item_size);
static int mem_queue_destroy(mem_queue_t *q);
static int mem_queue_clear(mem_queue_t *q);
static int mem_queue_send(mem_queue_t *q, const void *item, uint32_t size, uint32_t timeout_ms);
static int mem_queue_receive(mem_queue_t *q, void *buffer, uint32_t size, uint32_t timeout_ms);
static uint32_t mem_queue_get_used_cnt(const mem_queue_t *q);
static uint32_t mem_queue_get_max_used_cnt(const mem_queue_t *q);
static uint32_t mem_queue_get_free_cnt(const mem_queue_t *q);
static int mem_queue_is_full(const mem_queue_t *q);
static int mem_queue_is_empty(const mem_queue_t *q);


//==============================================================================
// Internal Linked-List Implementation
//==============================================================================

static void ll_list_init(ll_list_t *list);
static int ll_list_push_back(ll_list_t *list, DataNode_t *data);
static int ll_list_remove(ll_list_t *list, const DataNode_t *data);
static DataNode_t* ll_list_find(ll_list_t *list, const char *name);
static void ll_list_clear(ll_list_t *list);

#define ll_list_for_each(list, node_p) \
    for (ll_node_t *node_p = (list)->head; node_p != NULL; node_p = (node_p)->next)


static void ll_list_init(ll_list_t *list) 
{
    if (!list) return;
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

static int ll_list_push_back(ll_list_t *list, DataNode_t *data)
{
    if (!list || !data) return -1;
#if DH_USE_STATIC_NODE_LIST
    ll_node_t *node = &node_priv(data)->static_node_self;
#else
    ll_node_t *node = Mem_alloc(sizeof(ll_node_t));
#endif
    if (!node) return -1;
    node->data = data;
    node->next = NULL;
    if (list->tail) {
        list->tail->next = node;
    } else {
        list->head = node;
    }
    list->tail = node;
    list->size++;
    return 0;
}

static int ll_list_remove(ll_list_t *list, const DataNode_t *data) 
{
    if (!list || !data) return -1;
    ll_node_t **pp = &list->head;
    ll_node_t *prev = NULL;
    
    while (*pp != NULL) {
        ll_node_t *current = *pp;
        if (current->data == data) {
            *pp = current->next;
            if (current == list->tail) {
                list->tail = prev;
            }
#if DH_USE_STATIC_NODE_LIST
            // Static node list doesn't need to free nodes
#else
            Mem_free(current);
#endif
            list->size--;
            return 0;
        }
        prev = current;
        pp = &current->next;
    }
    return -1;
}

static DataNode_t* ll_list_find(ll_list_t *list, const char *name) 
{
    if (!list || !name) return NULL;

    for (ll_node_t *node = list->head; node != NULL; node = node->next) 
    {
        if (strcmp(node->data->name, name) == 0) {
            return node->data;
        }
    }
    return NULL;
}

static void ll_list_clear(ll_list_t *list) 
{
    if (!list) return;

    ll_node_t *node = list->head;
    while (node) {
        ll_node_t *next = node->next;
        Mem_free(node);
        node = next;
    }
    ll_list_init(list);
}


//==============================================================================
// Hub API Implementation
//==============================================================================

DH_API int DataHub_Init(void) 
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&hub_p()->is_inited, &expected, true)) {
        return DH_ERR_INITIALIZED;
    }

    ll_list_init(&hub_p()->node_list);
    if (Rwlock_init(&hub_p()->list_lock) != 0) {
        atomic_store(&hub_p()->is_inited, false);
        return DH_ERR_FAIL;
    }

    // Initialize the dummy node
    int err = 0;
    
    if ((err = DataHub_InitNode(_dummyNode)) != DH_OK) {
        return err;
    }

    // Push the dummy node to the hub's node list
    if ((err = DataHub_PushBackNode(_dummyNode)) != DH_OK) {
        DataHub_DeinitNode(_dummyNode);
        return err;
    }

#if USE_ASYNC_EVENT
#if ASYNC_EVENT_QUEUE_STATIC
    mem_queue_create_static(&hub_p()->async_pub_queue, 
        s_queue_buffer, ASYNC_EVENT_QUEUE_SIZE, sizeof(AsyncEvent_t));
#else
    mem_queue_create(&hub_p()->async_pub_queue, 
        ASYNC_EVENT_QUEUE_SIZE, sizeof(AsyncEvent_t));
#endif
#endif

    return DH_OK;
}

DH_API int DataHub_Deinit(void) 
{
    bool expected = true;
    if (!atomic_compare_exchange_strong(&hub_p()->is_inited, &expected, false)) {
        return DH_ERR_NOTINITIALIZED;
    }

    Rwlock_wrlock(&hub_p()->list_lock);
    ll_list_for_each(&hub_p()->node_list, node) {
        DataHub_DeinitNode(node->data);
    }
    ll_list_clear(&hub_p()->node_list);
    Rwlock_unlock(&hub_p()->list_lock);
    Rwlock_destroy(&hub_p()->list_lock);
    return DH_OK;
}

DH_API int DataHub_GetNodeNum(void) 
{
    if (!atomic_load(&hub_p()->is_inited)) return DH_ERR_NOTINITIALIZED;

    Rwlock_rdlock(&hub_p()->list_lock);
    int size = hub_p()->node_list.size;
    Rwlock_unlock(&hub_p()->list_lock);
    return size;
}

DH_API DataNode_t *DataHub_SearchNode(const char *name) 
{
    if (!atomic_load(&hub_p()->is_inited) || !name) return NULL;

    Rwlock_rdlock(&hub_p()->list_lock);
    DataNode_t *result = ll_list_find(&hub_p()->node_list, name);
    Rwlock_unlock(&hub_p()->list_lock);
    return result;
}

DH_API const char *DataHub_GetErrStr(int err) 
{
    switch (err) {
        case DH_OK:                 return "OK";
        case DH_ERR_FAIL:           return "Fail";
        case DH_ERR_TIMEOUT:        return "Timeout";
        case DH_ERR_NOMEM:          return "No Memory";
        case DH_ERR_NOTFOUND:       return "Not Found";
        case DH_ERR_NOSUPPORT:      return "Not Supported";
        case DH_ERR_BUSY:           return "Busy";
        case DH_ERR_INVALID:        return "Invalid Argument";
        case DH_ERR_ACCESS:         return "Access Denied";
        case DH_ERR_EXIST:          return "Already Exists";
        case DH_ERR_NODATA:         return "No Data Available";
        case DH_ERR_INITIALIZED:    return "Already Initialized";
        case DH_ERR_NOTINITIALIZED: return "Not Initialized";
        case DH_ERR_SIZE_MISMATCH:  return "Data Size Mismatch";
        default:                    return "Unknown Error";
    }
}

// static int validate_op_hub(void) {
//     if (!atomic_load(&hub_p()->is_inited)) return DH_ERR_NOTINITIALIZED;
//     return DH_OK;
// }

// static int validate_op_node(const DataNode_t *node_p) {
//     if (node_p == NULL || node_p->name[0] == '\0') return DH_ERR_INVALID;
//     if (!atomic_load(&node_priv(node_p)->is_inited)) return DH_ERR_NOTINITIALIZED;
//     return DH_OK;
// }

//==============================================================================
// Node Tool API Implementation
//==============================================================================

DH_API int DataHub_InitNode(DataNode_t *node_p) 
{
    if (!node_p || node_p->name[0] == '\0') return DH_ERR_INVALID;
    if ((node_p->conflags & CONF_CACHED) && node_p->size == 0) 
        return DH_ERR_INVALID; // Cached nodes must have a defined size
    if ((node_p->conflags & CONF_ASYNC) && !(node_p->conflags & CONF_CACHED))
        return DH_ERR_NOSUPPORT; // Async nodes must be cached

    struct DataNodePriv* priv = node_priv(node_p);
    bool expected = false;
    if (!atomic_compare_exchange_strong(&priv->is_inited, &expected, true)) {
        return DH_ERR_INITIALIZED; // Node already initialized
    }
    
    memset(node_p->priv, 0, DATAHUB_PRIV_DATA_SIZE);
    atomic_store(&priv->is_inited, true);
    atomic_store(&priv->is_registered, false);
    ll_list_init(&priv->subscribers);
    ll_list_init(&priv->subscriptions);
    Mutex_init(&priv->subscribers_lock);
    Mutex_init(&priv->subscriptions_lock);
    Rwlock_init(&priv->cache_lock);
    priv->cache_p = NULL;

    // Handle cache allocation if requested
    if (node_p->conflags & CONF_CACHED) 
    {
        priv->cache_p = Mem_alloc(node_p->size);
        if (!priv->cache_p) {
            atomic_store(&priv->is_inited, false);
            return DH_ERR_NOMEM;
        }
        memset(priv->cache_p, 0, node_p->size);
    }

    return DH_OK;
}

DH_API int DataHub_DeinitNode(DataNode_t *node_p) 
{
    if (!node_p) return DH_ERR_INVALID;
    struct DataNodePriv* priv = node_priv(node_p);
    bool expected = true;
    if (!atomic_compare_exchange_strong(&priv->is_inited, &expected, false)) {
        return DH_ERR_NOTINITIALIZED; // Node not initialized
    }

    if (atomic_load(&priv->is_registered)) {
        DataHub_RemoveNode(node_p);
    }
    
    if (priv->cache_p) {
        Mem_free(priv->cache_p);
        priv->cache_p = NULL;
    }
    Mutex_destroy(&priv->subscribers_lock);
    Mutex_destroy(&priv->subscriptions_lock);
    Rwlock_destroy(&priv->cache_lock);
    ll_list_clear(&priv->subscribers);
    ll_list_clear(&priv->subscriptions);
    
    return DH_OK;
}

DH_API int DataHub_PushBackNode(DataNode_t *node_p) 
{
    if (!node_p) return DH_ERR_INVALID;
    if (!atomic_load(&hub_p()->is_inited)) return DH_ERR_NOTINITIALIZED;
    if (!node_priv(node_p)->is_inited) return DH_ERR_NOTINITIALIZED;

    bool expected = false;
    if (!atomic_compare_exchange_strong(&node_priv(node_p)->is_registered, &expected, true)) {
        return DH_ERR_EXIST;
    }

    Rwlock_wrlock(&hub_p()->list_lock);
    if (ll_list_find(&hub_p()->node_list, node_p->name)) {
        Rwlock_unlock(&hub_p()->list_lock);
        atomic_store(&node_priv(node_p)->is_registered, false);
        return DH_ERR_EXIST;
    }
    int ret = ll_list_push_back(&hub_p()->node_list, node_p);
    Rwlock_unlock(&hub_p()->list_lock);

    if (ret != 0) {
        atomic_store(&node_priv(node_p)->is_registered, false);
        return DH_ERR_NOMEM;
    }
    return DH_OK;
}

DH_API int DataHub_RemoveNode(DataNode_t *node_p) 
{
    if (!node_p) return DH_ERR_INVALID;
    if (!atomic_load(&hub_p()->is_inited)) return DH_ERR_NOTINITIALIZED;
    bool expected = true;
    if (!atomic_compare_exchange_strong(&node_priv(node_p)->is_registered, &expected, false)) {
        return DH_ERR_NOTFOUND;
    }

    Rwlock_wrlock(&hub_p()->list_lock);
    int ret = ll_list_remove(&hub_p()->node_list, node_p);
    Rwlock_unlock(&hub_p()->list_lock);

    return (ret == 0) ? DH_OK : DH_ERR_NOTFOUND;
}

DH_API int DataHub_GetNodePubNum(DataNode_t *node_p) 
{
    if (!node_p || !node_priv(node_p)->is_inited) return DH_ERR_INVALID;

    Mutex_lock(&node_priv(node_p)->subscribers_lock);
    int size = node_priv(node_p)->subscribers.size;
    Mutex_unlock(&node_priv(node_p)->subscribers_lock);
    return size;
}

DH_API int DataHub_GetNodeSubNum(DataNode_t *node_p) 
{
    if (!node_p || !node_priv(node_p)->is_inited) return DH_ERR_INVALID;

    Mutex_lock(&node_priv(node_p)->subscriptions_lock);
    int size = node_priv(node_p)->subscriptions.size;
    Mutex_unlock(&node_priv(node_p)->subscriptions_lock);
    return size;
}

//==============================================================================
// Communication API Implementation
//==============================================================================

DH_API int DataHub_NodeSubscribe(DataNode_t *node_p, const char *name) 
{
    if (!node_p || !name) return DH_ERR_INVALID;
    if (!atomic_load(&hub_p()->is_inited)) return DH_ERR_NOTINITIALIZED;
    if (!atomic_load(&node_priv(node_p)->is_registered)) return DH_ERR_NOTFOUND;

    // Cannot subscribe to nodes without publish/publish_signal events
    EventCode_t check_mask = node_p->event_msk;
    if ((check_mask & (EVENT_PUBLISH | EVENT_PUBLISH_SIG)) == 0) {
        return DH_ERR_NOSUPPORT; 
    }

    DataNode_t *pub_node = DataHub_SearchNode(name);
    if (!pub_node) return DH_ERR_NOTFOUND;
    if (node_p == pub_node) return DH_ERR_INVALID; // Cannot subscribe to self

    // always lock nodes in a consistent order (by address)
    Mutex_t *lock1, *lock2;
    if ((uintptr_t)node_p < (uintptr_t)pub_node) {
        lock1 = &node_priv(node_p)->subscriptions_lock;
        lock2 = &node_priv(pub_node)->subscribers_lock;
    } else {
        lock1 = &node_priv(pub_node)->subscribers_lock;
        lock2 = &node_priv(node_p)->subscriptions_lock;
    }

    Mutex_lock(lock1);
    Mutex_lock(lock2);

    int ret = DH_OK;
    if (ll_list_find(&node_priv(node_p)->subscriptions, pub_node->name)) 
    {
        ret = DH_ERR_EXIST;
    } 
    else 
    {
        if (ll_list_push_back(&node_priv(node_p)->subscriptions, pub_node) == 0) {
            ll_list_push_back(&node_priv(pub_node)->subscribers, node_p);
        } else {
            ret = DH_ERR_NOMEM;
        }
    }

    Mutex_unlock(lock2);
    Mutex_unlock(lock1);
    return ret;
}

DH_API int DataHub_NodeUnsubscribe(DataNode_t *node_p, const char *name) 
{
    if (!node_p || !name) return DH_ERR_INVALID;
    if (!atomic_load(&hub_p()->is_inited)) return DH_ERR_NOTINITIALIZED;
    if (!atomic_load(&node_priv(node_p)->is_registered)) return DH_ERR_NOTFOUND;

    DataNode_t *pub_node = DataHub_SearchNode(name);
    if (!pub_node) return DH_ERR_NOTFOUND;

    Mutex_t *lock1, *lock2;
    if ((uintptr_t)node_p < (uintptr_t)pub_node) {
        lock1 = &node_priv(node_p)->subscriptions_lock;
        lock2 = &node_priv(pub_node)->subscribers_lock;
    } else {
        lock1 = &node_priv(pub_node)->subscribers_lock;
        lock2 = &node_priv(node_p)->subscriptions_lock;
    }

    Mutex_lock(lock1);
    Mutex_lock(lock2);

    int ret = DH_OK;
    if (ll_list_remove(&node_priv(node_p)->subscriptions, pub_node) == 0) {
        ll_list_remove(&node_priv(pub_node)->subscribers, node_p);
    } else {
        ret = DH_ERR_NOTFOUND;
    }

    Mutex_unlock(lock2);
    Mutex_unlock(lock1);
    return ret;
}


static int SendEvent(struct DataNode* node_p, EventParam_t* param)
{
    if (!node_p || !param) return DH_ERR_INVALID;
    if (!(node_p->event_msk & param->event)) return DH_ERR_NOSUPPORT;
    
    // check if CONF_CACHED is enabled
    if (param->event == EVENT_PULL)
    {
        bool route2cache = (node_p->conflags & CONF_CACHED) && node_priv(node_p)->cache_p;

        if (route2cache)
        {
            if (!param->data_p || param->size != node_p->size) return DH_ERR_SIZE_MISMATCH;
            
            Rwlock_rdlock(&node_priv(node_p)->cache_lock);
            memcpy(param->data_p, node_priv(node_p)->cache_p, node_p->size);
            Rwlock_unlock(&node_priv(node_p)->cache_lock);
            return DH_OK;
        }
    }

    if (!node_p->event_cb) return DH_ERR_FAIL;
    return node_p->event_cb(node_p, param);
}

static int node_publish(DataNode_t *node_p, const void *data_p, int size, int just_signal)
{
    struct DataNodePriv* priv = node_priv(node_p);

    // update the cache first.
    if ((node_p->conflags & CONF_CACHED) && priv->cache_p) 
    {
        Rwlock_wrlock(&priv->cache_lock);
        memcpy(priv->cache_p, data_p, node_p->size);
        Rwlock_unlock(&priv->cache_lock);
    }

    if (node_p->conflags & CONF_ASYNC) 
    {
        // If the node is async, we need to queue the event
        AsyncEvent_t *event = Mem_alloc(sizeof(AsyncEvent_t));
        if (!event) return DH_ERR_NOMEM;

        event->node_p = node_p;
        event->event = just_signal ? EVENT_PUBLISH_SIG : EVENT_PUBLISH;

        Mutex_lock(&hub_p()->async_pub_queue.lock);
        ll_list_push_back(&hub_p()->async_pub_queue.events, event);
        Mutex_unlock(&hub_p()->async_pub_queue.lock);
        return DH_OK;
    }

    // Determine the event type
    void *data_p_cast = just_signal ? NULL : (void*)data_p;
    uint32_t size_cast = just_signal ? 0 : (uint32_t)size;
    EventCode_t event_type = just_signal ? EVENT_PUBLISH_SIG : EVENT_PUBLISH;

    // notify all subscribers.
    Mutex_lock(&priv->subscribers_lock);
    ll_list_for_each(&priv->subscribers, sub_ll_node) 
    {
        DataNode_t* sub_node = sub_ll_node->data;
        if (sub_node == NULL) continue;

        const int supported = sub_node->event_cb && \
                            (sub_node->event_msk & event_type);
        if (!supported) continue;

            EventParam_t param = {
                .event = event_type,
                .sender = node_p,
                .recver = sub_node,
                .data_p = data_p_cast,
                .size = size_cast,
            };

            SendEvent(sub_node, &param);
    }
    Mutex_unlock(&priv->subscribers_lock);

    return DH_OK;
}

DH_API int DataHub_NodePublish(DataNode_t *node_p, const void *data_p, int size)
{
    if (!node_p || !data_p || size < 0) return DH_ERR_INVALID;
    if (!atomic_load(&node_priv(node_p)->is_registered)) return DH_ERR_NOTFOUND;
    if (node_p->size != 0 && node_p->size != (uint32_t)size) return DH_ERR_SIZE_MISMATCH;

    return node_publish(node_p, data_p, size, 0); // publish with data
}

DH_API int DataHub_NodePublishSignal(DataNode_t *node_p, const void *data_p, int size)
{
    if (!node_p || !data_p || size < 0) return DH_ERR_INVALID;
    if (!atomic_load(&node_priv(node_p)->is_registered)) return DH_ERR_NOTFOUND;
    if (node_p->size != 0 && node_p->size != (uint32_t)size) return DH_ERR_SIZE_MISMATCH;

    return node_publish(node_p, data_p, size, 1); // publish just signal
}

DH_API int DataHub_NodePull(DataNode_t *node_p, const char *name, void *data_p, uint32_t size) 
{
    if (!node_p || !name || !data_p) return DH_ERR_INVALID;
    if (!atomic_load(&hub_p()->is_inited)) return DH_ERR_NOTINITIALIZED;
    if (!atomic_load(&node_priv(node_p)->is_registered)) return DH_ERR_NOTFOUND;

    DataNode_t *pub_node = DataHub_SearchNode(name);
    if (!pub_node) return DH_ERR_NOTFOUND;
    if (pub_node->size != 0 && pub_node->size != size) return DH_ERR_SIZE_MISMATCH;

    // check if the target node supports the PULL event
    if (!(pub_node->event_msk & EVENT_PULL)) {
        return DH_ERR_NOSUPPORT;
    }

    EventParam_t param = {
        .event = EVENT_PULL,
        .sender = node_p,
        .recver = pub_node,
        .data_p = data_p, // caller's buffer
        .size = size,
    };

    return SendEvent(pub_node, &param);
}

DH_API int DataHub_NodeNotify(DataNode_t *node_p, const char *name, const void *data_p, int size) 
{
    if (!node_p || !name || !data_p || size < 0) return DH_ERR_INVALID;
    if (!atomic_load(&hub_p()->is_inited)) return DH_ERR_NOTINITIALIZED;
    if (!atomic_load(&node_priv(node_p)->is_registered)) return DH_ERR_NOTFOUND;

    DataNode_t *target_node = DataHub_SearchNode(name);
    if (!target_node) return DH_ERR_NOTFOUND;

    EventParam_t param = {
        .event = EVENT_NOTIFY,
        .sender = node_p,
        .recver = target_node,
        .data_p = (void*)data_p,
        .size = (uint32_t)size,
    };
    
    return SendEvent(target_node, &param);;
}

DH_API int DataHub_GetAsyncEventQueueMaxUsed(void)
{
    return mem_queue_get_max_used_cnt(&hub_p()->async_pub_queue);
}

DH_API int DataHub_AsyncPoll()
{
    AsyncEvent_t aevent = {0};

    // check async publish queue
    int has_event = 0;
    has_event = mem_queue_receive(&hub_p()->async_pub_queue, &aevent, sizeof(AsyncEvent_t), 10);
    if (has_event == 0) return -1;

    // get queue item
    struct DataNodePriv * const pub_priv = node_priv(aevent.node_p);
    
    // build EventParam_t
    struct {void *data_p; uint32_t size;} pub_data = {0};

    switch (aevent.event) 
    {
        case EVENT_PUBLISH:
            pub_data.data_p = pub_priv->cache_p;
            pub_data.size = aevent.node_p->size;
            break;
        case EVENT_PUBLISH_SIG:
        default:
            pub_data.data_p = NULL; // no data for signal
            pub_data.size = 0; // no size for signal
            break;
    }

    EventParam_t param = {
        .event = aevent.event,
        .sender = aevent.node_p,
        .recver = NULL, // will be set in the loop
        .data_p = pub_data.data_p, // use cache if available
        .size = pub_data.size,
    };

    // traverse node_p->subscribers
    // call event_cb with EVENT_PUBLISH or EVENT_PUBLISH_SIG
    Mutex_lock(&pub_priv->subscribers_lock);
    ll_list_for_each(&pub_priv->subscribers, sub_ll_node) 
    {
        const DataNode_t * const sub_node = sub_ll_node->data;

        if (sub_node && sub_node->event_cb && (sub_node->event_msk & aevent.event)) 
        {
            param.recver = sub_node; // set the receiver
            SendEvent(sub_node, &param);
        }
    }
    Mutex_unlock(&pub_priv->subscribers_lock);
}
