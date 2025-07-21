#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "DataHub.h"

//==============================================================================
// Error Codes
//==============================================================================

#define ERROR_CODES() \
    ERROR_CODE(DH_OK, "Success") \
    ERROR_CODE(DH_ERR_FAIL, "General failure") \
    ERROR_CODE(DH_ERR_TIMEOUT, "Timeout") \
    ERROR_CODE(DH_ERR_NOMEM, "No memory") \
    ERROR_CODE(DH_ERR_NOTFOUND, "Not found") \
    ERROR_CODE(DH_ERR_NOSUPPORT, "Not supported") \
    ERROR_CODE(DH_ERR_BUSY, "Busy") \
    ERROR_CODE(DH_ERR_INVALID, "Invalid argument") \
    ERROR_CODE(DH_ERR_ACCESS, "Access denied") \
    ERROR_CODE(DH_ERR_EXIST, "Already exists") \
    ERROR_CODE(DH_ERR_NODATA, "No data available") \
    ERROR_CODE(DH_ERR_INITIALIZED, "Already initialized") \
    ERROR_CODE(DH_ERR_NOTINITIALIZED, "Not initialized") \
    ERROR_CODE(DH_ERR_SIZE_MISMATCH, "Size mismatch") \
    ERROR_CODE(DH_ERR_NULL_POINTER, "Null pointer") \

//==============================================================================
// Platform Abstraction Layer (PAL) for OS primitives
//==============================================================================

#include <pthread.h>
#define Mem_alloc(size)   malloc(size)
#define Mem_free(ptr)     free(ptr)

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

struct DataNodePriv {
    atomic_bool  is_inited;
    atomic_bool  is_registered;
    ll_list_t    subscribers;
    Mutex_t      subscribers_lock;
    ll_list_t    subscriptions;
    Mutex_t      subscriptions_lock;
#if DH_CACHE_SUPPORT_ENABLE
    void*        cache_p;
    Rwlock_t     cache_lock;
#endif
};

typedef \
struct DataHub {
    char          name[DH_NODE_NAME_MAX_LEN];
    ll_list_t     node_list;
    Rwlock_t      list_lock;
    atomic_bool   is_inited;
} DataHub_t;

static DataHub_t s_hub = {
    .name = "__DataHub__",
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
    .name = "__DummyNode__",
    .conflags = CONF_NONE,
    .event_msk = EVENT_NONE,
    .event_cb = NULL, // No event callback
    .user_data = NULL, // No user data
    .size = 0,
};

DataNode_t * const _dummyNode = &s_dummyNode;


//==============================================================================
// Internal Linked-List Implementation
//==============================================================================

static int ll_list_init(ll_list_t *list) 
{
    if (!list) return -1;
    list->head = list->tail = NULL;
    list->size = 0;
    return 0;
}

static int ll_list_push_back(ll_list_t *list, DataNode_t *data) 
{
    if (!list || !data) return -1;
    ll_node_t *node = Mem_alloc(sizeof(ll_node_t));
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
            Mem_free(current);
            list->size--;
            return 0;
        }
        prev = current;
        pp = &current->next;
    }
    return -1;
}

static DataNode_t *ll_list_find(ll_list_t *list, const char *name) 
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

#define ll_list_for_each(list, node_p) \
    for (ll_node_t *node_p = (list)->head; node_p != NULL; node_p = (node_p)->next)

//==============================================================================
// Internal Check Helpers
//==============================================================================

static inline int check_hub_inited(void) 
{
    if (!atomic_load(&hub_p()->is_inited)) 
        return DH_ERR_NOTINITIALIZED;
    return DH_OK;
}

static inline int check_node_inited(const DataNode_t *node_p) 
{
    if (!atomic_load(&node_priv(node_p)->is_inited)) 
        return DH_ERR_NOTINITIALIZED;
    return DH_OK;
}

static inline int check_node_registered(const DataNode_t *node_p) 
{
    if (!atomic_load(&node_priv(node_p)->is_registered)) 
        return DH_ERR_NOTFOUND;
    return DH_OK;
}

static inline int check_hub_and_node_work(const DataNode_t *node_p) 
{
    int ret = DH_OK;
    if ((ret = check_hub_inited()) != DH_OK) return ret;
    if ((ret = check_node_inited(node_p)) != DH_OK) return ret;
    if ((ret = check_node_registered(node_p)) != DH_OK) return ret;
    return DH_OK;
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
    Rwlock_wrunlock(&hub_p()->list_lock);
    Rwlock_destroy(&hub_p()->list_lock);
    return DH_OK;
}

DH_API int DataHub_GetNodeNum(void) 
{
    int err = DH_OK;
    if ((err = check_hub_inited()) != DH_OK) return err;

    Rwlock_rdlock(&hub_p()->list_lock);
    int size = hub_p()->node_list.size;
    Rwlock_rdunlock(&hub_p()->list_lock);
    return size;
}

DH_API DataNode_t *DataHub_SearchNode(const char *name) 
{
    if (name == NULL) return NULL;
    if (check_hub_inited() != DH_OK) return NULL;

    Rwlock_rdlock(&hub_p()->list_lock);
    DataNode_t *result = ll_list_find(&hub_p()->node_list, name);
    Rwlock_rdunlock(&hub_p()->list_lock);
    return result;
}

DH_API const char *DataHub_GetErrStr(int err) 
{
#define ERROR_CODE(__code__, __str__) \
    static const char s_##__code__##_STRING[] = __str__;
    ERROR_CODES()
#undef ERROR_CODE

    switch (err) 
    {
#define ERROR_CODE(__code__, __str__) \
        case __code__: return s_##__code__##_STRING;
        ERROR_CODES()
#undef ERROR_CODE

        default: 
            return "Unknown Error";
    }

#ifdef ERROR_CODE
    #undef ERROR_CODE
#endif
}

//==============================================================================
// Node Tool API Implementation
//==============================================================================

DH_API int DataHub_InitNode(DataNode_t *node_p)
{
    if (!node_p) return DH_ERR_NULL_POINTER;
    if (node_p->name[0] == '\0') return DH_ERR_INVALID;

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

#if DH_CACHE_SUPPORT_ENABLE
    Rwlock_init(&priv->cache_lock);
    priv->cache_p = NULL;

    // Handle cache allocation if requested
    if (node_p->conflags & CONF_CACHED) 
    {
        if (node_p->size == 0) {
            atomic_store(&priv->is_inited, false);
            return DH_ERR_INVALID; // Cached nodes must have a defined size
        }

        priv->cache_p = Mem_alloc(node_p->size);
        if (!priv->cache_p) {
            atomic_store(&priv->is_inited, false);
            return DH_ERR_NOMEM;
        }
        memset(priv->cache_p, 0, node_p->size);
    }
#else 
    if (node_p->conflags & CONF_CACHED) {
        atomic_store(&priv->is_inited, false);
        return DH_ERR_NOSUPPORT; // Cached nodes not supported
    }
#endif

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

#if DH_CACHE_SUPPORT_ENABLE
    if (priv->cache_p) {
        Mem_free(priv->cache_p);
        priv->cache_p = NULL;
    }
    Rwlock_destroy(&priv->cache_lock);
#endif

    Mutex_destroy(&priv->subscribers_lock);
    Mutex_destroy(&priv->subscriptions_lock);
    ll_list_clear(&priv->subscribers);
    ll_list_clear(&priv->subscriptions);
    
    return DH_OK;
}

DH_API int DataHub_PushBackNode(DataNode_t *node_p) 
{
    if (!node_p) return DH_ERR_INVALID;
    if (check_hub_inited() != DH_OK) return DH_ERR_NOTINITIALIZED;
    if (check_node_inited(node_p) != DH_OK) return DH_ERR_NOTINITIALIZED;
    if (check_node_registered(node_p) == DH_OK) return DH_ERR_EXIST;

    bool expected = false;
    if (!atomic_compare_exchange_strong(&node_priv(node_p)->is_registered, &expected, true)) {
        return DH_ERR_EXIST;
    }

    Rwlock_wrlock(&hub_p()->list_lock);
    if (ll_list_find(&hub_p()->node_list, node_p->name)) {
        Rwlock_wrunlock(&hub_p()->list_lock);
        atomic_store(&node_priv(node_p)->is_registered, false);
        return DH_ERR_EXIST;
    }
    int ret = ll_list_push_back(&hub_p()->node_list, node_p);
    Rwlock_wrunlock(&hub_p()->list_lock);

    if (ret != 0) {
        atomic_store(&node_priv(node_p)->is_registered, false);
        return DH_ERR_NOMEM;
    }
    return DH_OK;
}

DH_API int DataHub_RemoveNode(DataNode_t *node_p) 
{
    if (!node_p) return DH_ERR_INVALID;
    int err = DH_OK;
    if ((err = check_hub_and_node_work(node_p)) != DH_OK) return err;

    bool expected = true;
    if (!atomic_compare_exchange_strong(&node_priv(node_p)->is_registered, &expected, false)) {
        return DH_ERR_NOTFOUND;
    }

    Rwlock_wrlock(&hub_p()->list_lock);
    int ret = ll_list_remove(&hub_p()->node_list, node_p);
    Rwlock_wrunlock(&hub_p()->list_lock);

    return (ret == 0) ? DH_OK : DH_ERR_NOTFOUND;
}

DH_API int DataHub_GetNodePubNum(DataNode_t *node_p) 
{
    if (!node_p) return DH_ERR_NULL_POINTER;
    if (check_node_inited(node_p) != DH_OK) return DH_ERR_NOTINITIALIZED;

    Mutex_lock(&node_priv(node_p)->subscribers_lock);
    int size = node_priv(node_p)->subscribers.size;
    Mutex_unlock(&node_priv(node_p)->subscribers_lock);
    return size;
}

DH_API int DataHub_GetNodeSubNum(DataNode_t *node_p) 
{
    if (!node_p) return DH_ERR_NULL_POINTER;
    if (check_node_inited(node_p) != DH_OK) return DH_ERR_NOTINITIALIZED;

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
    int err = DH_OK;
    if ((err = check_hub_and_node_work(node_p)) != DH_OK) return err;

    /* avoid setting up a useless subscription */
    const EventCode_t check_mask = node_p->event_msk;
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
    if (!ll_list_find(&node_priv(node_p)->subscriptions, pub_node->name)) 
    {
        if (ll_list_push_back(&node_priv(node_p)->subscriptions, pub_node) == 0) {
            ll_list_push_back(&node_priv(pub_node)->subscribers, node_p);
        } else {
            ret = DH_ERR_NOMEM;
        }
    } 
    else {
        ret = DH_ERR_EXIST;
    }

    Mutex_unlock(lock2);
    Mutex_unlock(lock1);
    return ret;
}

DH_API int DataHub_NodeUnsubscribe(DataNode_t *node_p, const char *name)
{
    if (!node_p) return DH_ERR_NULL_POINTER;
    if (!name || name[0] == '\0') return DH_ERR_INVALID;
    int err = DH_OK;
    if ((err = check_hub_and_node_work(node_p)) != DH_OK) return err;

    DataNode_t *pub_node = DataHub_SearchNode(name);
    if (pub_node == NULL) return DH_ERR_NOTFOUND;

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

#if DH_CACHE_SUPPORT_ENABLE
static inline 
void copy_from_cache(DataNode_t *node_p, void *data_p, int size)
{
    Rwlock_rdlock(&node_priv(node_p)->cache_lock);
    memcpy(data_p, node_priv(node_p)->cache_p, size);
    Rwlock_rdunlock(&node_priv(node_p)->cache_lock);
}

static inline 
void update_to_cache(DataNode_t *node_p, const void *data_p, int size)
{
    Rwlock_wrlock(&node_priv(node_p)->cache_lock);
    memcpy(node_priv(node_p)->cache_p, data_p, size);
    Rwlock_wrunlock(&node_priv(node_p)->cache_lock);
}
#endif

static int SendEvent(struct DataNode* node_p, EventParam_t* param)
{
#if DH_NODE_COMMUNICATION_LOG_ENABLE
    static const char *_event_str[] = {
        "EVENT_NONE",
        "EVENT_PUBLISH",
        "EVENT_PULL",
        "EVENT_NOTIFY",
        "EVENT_PUBLISH_SIG",
    };

    DH_NODE_COMM_LOG(
        "Comm Event Flow: sender=%s --<event:%s>--> recver=%s, size=%d", 
        param->sender->name, 
        _event_str[param->event], 
        param->recver->name, 
        param->size
    );

#endif

#if DH_CACHE_SUPPORT_ENABLE
    // check if CONF_CACHED is enabled
    if (param->event == EVENT_PULL) {
        struct DataNodePriv * const priv = node_priv(node_p);

        if ((node_p->conflags & CONF_CACHED) && priv->cache_p) {
            copy_from_cache(node_p, param->data_p, param->size);
            return DH_OK;
        }
    }
#endif

    if (!node_p->event_cb) return DH_ERR_FAIL;
    return node_p->event_cb(node_p, param);
}

static int node_publish(DataNode_t *node_p, const void *data_p, int size, int just_signal)
{
    struct DataNodePriv * const priv = node_priv(node_p);

#if DH_CACHE_SUPPORT_ENABLE
    // update the cache first.
    if ((node_p->conflags & CONF_CACHED) && priv->cache_p) {
        update_to_cache(node_p, data_p, size);
    }
#endif

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
    int err = DH_OK;
    if ((err = check_hub_and_node_work(node_p)) != DH_OK) return err;

    if (node_p->size != 0 && node_p->size != (uint32_t)size) 
        return DH_ERR_SIZE_MISMATCH;

    return node_publish(node_p, data_p, size, 0); // publish with data
}

DH_API int DataHub_NodePublishSignal(DataNode_t *node_p, const void *data_p, int size)
{
    if (!node_p || !data_p || size < 0) return DH_ERR_INVALID;
    int err = DH_OK;
    if ((err = check_hub_and_node_work(node_p)) != DH_OK) return err;
    if (node_p->size != 0 && node_p->size != (uint32_t)size) 
        return DH_ERR_SIZE_MISMATCH;

    return node_publish(node_p, data_p, size, 1); // publish just signal
}

DH_API int DataHub_NodePull(DataNode_t *node_p, const char *name, void *data_p, uint32_t size) 
{
    if (!node_p || !name || !data_p) return DH_ERR_INVALID;
    int err = DH_OK;
    if ((err = check_hub_and_node_work(node_p)) != DH_OK) return err;

    DataNode_t *pub_node = DataHub_SearchNode(name);
    if (!pub_node) return DH_ERR_NOTFOUND;
    if (pub_node->size != size) return DH_ERR_SIZE_MISMATCH;

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
    int err = DH_OK;
    if ((err = check_hub_and_node_work(node_p)) != DH_OK) return err;

    DataNode_t *target_node = DataHub_SearchNode(name);
    if (!target_node) return DH_ERR_NOTFOUND;

#if DH_RESTRICT_NOTIFY_SIZE_CHECK_ENABLE
    if (size != target_node->notify_size) return DH_ERR_SIZE_MISMATCH;
#endif

    // check if the target node supports the NOTIFY event
    if (!(target_node->event_msk & EVENT_NOTIFY)) {
        return DH_ERR_NOSUPPORT;
    }

    EventParam_t param = {
        .event = EVENT_NOTIFY,
        .sender = node_p,
        .recver = target_node,
        .data_p = (void*)data_p,
        .size = (uint32_t)size,
    };
    
    return SendEvent(target_node, &param);;
}
