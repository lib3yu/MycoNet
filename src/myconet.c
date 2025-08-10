#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <myconet.h>
#include <myconet_conf.h>
#include <myconet_port.h>

//==============================================================================
// Error Codes
//==============================================================================

#define ERROR_CODES() \
    ERROR_CODE(MN_OK, "Success") \
    ERROR_CODE(MN_ERR_FAIL, "General failure") \
    ERROR_CODE(MN_ERR_TIMEOUT, "Timeout") \
    ERROR_CODE(MN_ERR_NOMEM, "No memory") \
    ERROR_CODE(MN_ERR_NOTFOUND, "Not found") \
    ERROR_CODE(MN_ERR_NOSUPPORT, "Not supported") \
    ERROR_CODE(MN_ERR_BUSY, "Busy") \
    ERROR_CODE(MN_ERR_INVALID, "Invalid argument") \
    ERROR_CODE(MN_ERR_ACCESS, "Access denied") \
    ERROR_CODE(MN_ERR_EXIST, "Already exists") \
    ERROR_CODE(MN_ERR_NODATA, "No data available") \
    ERROR_CODE(MN_ERR_INITIALIZED, "Already initialized") \
    ERROR_CODE(MN_ERR_NOTINITIALIZED, "Not initialized") \
    ERROR_CODE(MN_ERR_SIZE_MISMATCH, "Size mismatch") \
    ERROR_CODE(MN_ERR_NULL_POINTER, "Null pointer") \

//==============================================================================
// Platform Abstraction Layer (PAL) for OS primitives
//==============================================================================


typedef \
struct ll_node {
    struct ll_node *next;
    MycoNode_t     *data;
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
    ll_list_t    subscriptions;
#if MN_CACHE_SUPPORT_ENABLE
    void*        cache_p;
#endif /* MN_CACHE_SUPPORT_ENABLE */

#if MN_CONF_USE_LOCK
#if MN_CACHE_SUPPORT_ENABLE
    Rwlock_t     cache_lock;
#endif /* MN_CACHE_SUPPORT_ENABLE */
    Mutex_t      subscribers_lock;
    Mutex_t      subscriptions_lock;
#endif /* MN_CONF_USE_LOCK */

};

typedef \
struct DataHub {
    char          name[MN_NODE_NAME_MAX_LEN];
    ll_list_t     node_list;
    atomic_bool   is_inited;
#if MN_CONF_USE_LOCK
    Rwlock_t      list_lock;
#endif
} MycoNet_t;

static MycoNet_t s_hub = {
    .name = "__MycoNet__",
    .is_inited = ATOMIC_VAR_INIT(false),
};

// Static assertion to ensure private data fits into the reserved space
_Static_assert(sizeof(struct DataNodePriv) <= MYCONET_PRIV_DATA_SIZE,
               "MYCONET_PRIV_DATA_SIZE is too small for internal private data!");


#define node_priv(node_p) ((struct DataNodePriv *)((node_p)->priv))
#define hub_p() (&s_hub)

//==============================================================================
// Dummy Node Definition
//==============================================================================

static MycoNode_t s_dummyNode = {
    .name = "__DummyNode__",
    .conflags = CONF_NONE,
    .event_msk = EVENT_NONE,
    .event_cb = NULL, // No event callback
    .user_data = NULL, // No user data
    .size = 0,
};

MycoNode_t * const _dummyNode = &s_dummyNode;


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

static int ll_list_push_back(ll_list_t *list, MycoNode_t *data) 
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

static int ll_list_remove(ll_list_t *list, const MycoNode_t *data) 
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

static MycoNode_t *ll_list_find(ll_list_t *list, const char *name) 
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
        return MN_ERR_NOTINITIALIZED;
    return MN_OK;
}

static inline int check_node_inited(const MycoNode_t *node_p) 
{
    if (!atomic_load(&node_priv(node_p)->is_inited)) 
        return MN_ERR_NOTINITIALIZED;
    return MN_OK;
}

static inline int check_node_registered(const MycoNode_t *node_p) 
{
    if (!atomic_load(&node_priv(node_p)->is_registered)) 
        return MN_ERR_NOTFOUND;
    return MN_OK;
}

static inline int check_hub_and_node_work(const MycoNode_t *node_p) 
{
    int ret = MN_OK;
    if ((ret = check_hub_inited()) != MN_OK) return ret;
    if ((ret = check_node_inited(node_p)) != MN_OK) return ret;
    if ((ret = check_node_registered(node_p)) != MN_OK) return ret;
    return MN_OK;
}

//==============================================================================
// Hub API Implementation
//==============================================================================

MN_API int MycoNet_Init(void) 
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&hub_p()->is_inited, &expected, true)) {
        return MN_ERR_INITIALIZED;
    }

    ll_list_init(&hub_p()->node_list);

#if MN_CONF_USE_LOCK
    if (Rwlock_init(&hub_p()->list_lock) != 0) {
        atomic_store(&hub_p()->is_inited, false);
        return MN_ERR_FAIL;
    }
#endif

    // Initialize the dummy node
    int err = 0;
    
    if ((err = MycoNet_InitNode(_dummyNode)) != MN_OK) {
        return err;
    }

    // Push the dummy node to the hub's node list
    if ((err = MycoNet_PushBackNode(_dummyNode)) != MN_OK) {
        MycoNet_DeinitNode(_dummyNode);
        return err;
    }

    return MN_OK;
}

MN_API int MycoNet_Deinit(void) 
{
    bool expected = true;
    if (!atomic_compare_exchange_strong(&hub_p()->is_inited, &expected, false)) {
        return MN_ERR_NOTINITIALIZED;
    }

    Rwlock_wrlock(&hub_p()->list_lock);
    ll_list_for_each(&hub_p()->node_list, node) {
        MycoNet_DeinitNode(node->data);
    }
    ll_list_clear(&hub_p()->node_list);
    Rwlock_wrunlock(&hub_p()->list_lock);
    Rwlock_destroy(&hub_p()->list_lock);
    return MN_OK;
}

MN_API int MycoNet_GetNodeNum(void) 
{
    int err = MN_OK;
    if ((err = check_hub_inited()) != MN_OK) return err;

    Rwlock_rdlock(&hub_p()->list_lock);
    int size = hub_p()->node_list.size;
    Rwlock_rdunlock(&hub_p()->list_lock);
    return size;
}

MN_API MycoNode_t *MycoNet_SearchNode(const char *name) 
{
    if (name == NULL) return NULL;
    if (check_hub_inited() != MN_OK) return NULL;

    Rwlock_rdlock(&hub_p()->list_lock);
    MycoNode_t *result = ll_list_find(&hub_p()->node_list, name);
    Rwlock_rdunlock(&hub_p()->list_lock);
    return result;
}

MN_API const char *MycoNet_GetErrStr(int err) 
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

MN_API int MycoNet_PrintNodeList(int (*print_)(const char *fmt, ...))
{
    if (print_ == NULL) return MN_ERR_NULL_POINTER;
    if (check_hub_inited()) return MN_ERR_NOTINITIALIZED;

    Rwlock_rdlock(&hub_p()->list_lock);
    print_("Node List:\n");
    ll_list_for_each(&hub_p()->node_list, node_p) {
        print_("\t%s\n", node_p->data->name);
    }
    Rwlock_rdunlock(&hub_p()->list_lock);
    return MN_OK;
}

//==============================================================================
// Node Tool API Implementation
//==============================================================================

MN_API int MycoNet_InitNode(MycoNode_t *node_p)
{
    if (!node_p) return MN_ERR_NULL_POINTER;
    if (node_p->name[0] == '\0') return MN_ERR_INVALID;

    struct DataNodePriv* priv = node_priv(node_p);
    bool expected = false;
    if (!atomic_compare_exchange_strong(&priv->is_inited, &expected, true)) {
        return MN_ERR_INITIALIZED; // Node already initialized
    }
    
    memset(node_p->priv, 0, MYCONET_PRIV_DATA_SIZE);
    atomic_store(&priv->is_inited, true);
    atomic_store(&priv->is_registered, false);
    ll_list_init(&priv->subscribers);
    ll_list_init(&priv->subscriptions);
    Mutex_init(&priv->subscribers_lock);
    Mutex_init(&priv->subscriptions_lock);

#if MN_CACHE_SUPPORT_ENABLE
    Rwlock_init(&priv->cache_lock);
    priv->cache_p = NULL;

    // Handle cache allocation if requested
    if (node_p->conflags & CONF_CACHED) 
    {
        if (node_p->size == 0) {
            atomic_store(&priv->is_inited, false);
            return MN_ERR_INVALID; // Cached nodes must have a defined size
        }

        priv->cache_p = Mem_alloc(node_p->size);
        if (!priv->cache_p) {
            atomic_store(&priv->is_inited, false);
            return MN_ERR_NOMEM;
        }
        memset(priv->cache_p, 0, node_p->size);
    }
#else 
    if (node_p->conflags & CONF_CACHED) {
        atomic_store(&priv->is_inited, false);
        return MN_ERR_NOSUPPORT; // Cached nodes not supported
    }
#endif

    return MN_OK;
}

MN_API int MycoNet_DeinitNode(MycoNode_t *node_p) 
{
    if (!node_p) return MN_ERR_INVALID;
    struct DataNodePriv* priv = node_priv(node_p);
    bool expected = true;
    if (!atomic_compare_exchange_strong(&priv->is_inited, &expected, false)) {
        return MN_ERR_NOTINITIALIZED; // Node not initialized
    }

    if (atomic_load(&priv->is_registered)) {
        MycoNet_RemoveNode(node_p);
    }

#if MN_CACHE_SUPPORT_ENABLE
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
    
    return MN_OK;
}

MN_API int MycoNet_PushBackNode(MycoNode_t *node_p) 
{
    if (!node_p) return MN_ERR_INVALID;
    if (check_hub_inited() != MN_OK) return MN_ERR_NOTINITIALIZED;
    if (check_node_inited(node_p) != MN_OK) return MN_ERR_NOTINITIALIZED;
    if (check_node_registered(node_p) == MN_OK) return MN_ERR_EXIST;

    bool expected = false;
    if (!atomic_compare_exchange_strong(&node_priv(node_p)->is_registered, &expected, true)) {
        return MN_ERR_EXIST;
    }

    Rwlock_wrlock(&hub_p()->list_lock);
    if (ll_list_find(&hub_p()->node_list, node_p->name)) {
        Rwlock_wrunlock(&hub_p()->list_lock);
        atomic_store(&node_priv(node_p)->is_registered, false);
        return MN_ERR_EXIST;
    }
    int ret = ll_list_push_back(&hub_p()->node_list, node_p);
    Rwlock_wrunlock(&hub_p()->list_lock);

    if (ret != 0) {
        atomic_store(&node_priv(node_p)->is_registered, false);
        return MN_ERR_NOMEM;
    }
    return MN_OK;
}

MN_API int MycoNet_RemoveNode(MycoNode_t *node_p) 
{
    if (!node_p) return MN_ERR_INVALID;
    int err = MN_OK;
    if ((err = check_hub_and_node_work(node_p)) != MN_OK) return err;

    bool expected = true;
    if (!atomic_compare_exchange_strong(&node_priv(node_p)->is_registered, &expected, false)) {
        return MN_ERR_NOTFOUND;
    }

    Rwlock_wrlock(&hub_p()->list_lock);
    int ret = ll_list_remove(&hub_p()->node_list, node_p);
    Rwlock_wrunlock(&hub_p()->list_lock);

    return (ret == 0) ? MN_OK : MN_ERR_NOTFOUND;
}

MN_API int MycoNet_GetNodePubNum(MycoNode_t *node_p) 
{
    if (!node_p) return MN_ERR_NULL_POINTER;
    if (check_node_inited(node_p) != MN_OK) return MN_ERR_NOTINITIALIZED;

    Mutex_lock(&node_priv(node_p)->subscribers_lock);
    int size = node_priv(node_p)->subscribers.size;
    Mutex_unlock(&node_priv(node_p)->subscribers_lock);
    return size;
}

MN_API int MycoNet_GetNodeSubNum(MycoNode_t *node_p) 
{
    if (!node_p) return MN_ERR_NULL_POINTER;
    if (check_node_inited(node_p) != MN_OK) return MN_ERR_NOTINITIALIZED;

    Mutex_lock(&node_priv(node_p)->subscriptions_lock);
    int size = node_priv(node_p)->subscriptions.size;
    Mutex_unlock(&node_priv(node_p)->subscriptions_lock);
    return size;
}

//==============================================================================
// Communication API Implementation
//==============================================================================

MN_API int MycoNet_NodeSubscribe(MycoNode_t *node_p, const char *name) 
{
    if (!node_p || !name) return MN_ERR_INVALID;
    int err = MN_OK;
    if ((err = check_hub_and_node_work(node_p)) != MN_OK) return err;

    /* avoid setting up a useless subscription */
    const EventCode_t check_mask = node_p->event_msk;
    if ((check_mask & (EVENT_PUBLISH | EVENT_PUBLISH_SIG)) == 0) {
        return MN_ERR_NOSUPPORT; 
    }

    MycoNode_t *pub_node = MycoNet_SearchNode(name);
    if (!pub_node) return MN_ERR_NOTFOUND;
    if (node_p == pub_node) return MN_ERR_INVALID; // Cannot subscribe to self

    // always lock nodes in a consistent order (by address)
#if MN_CONF_USE_LOCK
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
#endif

    int ret = MN_OK;
    if (!ll_list_find(&node_priv(node_p)->subscriptions, pub_node->name)) 
    {
        if (ll_list_push_back(&node_priv(node_p)->subscriptions, pub_node) == 0) {
            ll_list_push_back(&node_priv(pub_node)->subscribers, node_p);
        } else {
            ret = MN_ERR_NOMEM;
        }
    } 
    else {
        ret = MN_ERR_EXIST;
    }

    Mutex_unlock(lock2);
    Mutex_unlock(lock1);
    return ret;
}

MN_API int MycoNet_NodeUnsubscribe(MycoNode_t *node_p, const char *name)
{
    if (!node_p) return MN_ERR_NULL_POINTER;
    if (!name || name[0] == '\0') return MN_ERR_INVALID;
    int err = MN_OK;
    if ((err = check_hub_and_node_work(node_p)) != MN_OK) return err;

    MycoNode_t *pub_node = MycoNet_SearchNode(name);
    if (pub_node == NULL) return MN_ERR_NOTFOUND;

#if MN_CONF_USE_LOCK
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
#endif

    int ret = MN_OK;
    if (ll_list_remove(&node_priv(node_p)->subscriptions, pub_node) == 0) {
        ll_list_remove(&node_priv(pub_node)->subscribers, node_p);
    } else {
        ret = MN_ERR_NOTFOUND;
    }

    Mutex_unlock(lock2);
    Mutex_unlock(lock1);
    return ret;
}

#if MN_CACHE_SUPPORT_ENABLE
static inline 
void copy_from_cache(MycoNode_t *node_p, void *data_p, int size)
{
    Rwlock_rdlock(&node_priv(node_p)->cache_lock);
    memcpy(data_p, node_priv(node_p)->cache_p, size);
    Rwlock_rdunlock(&node_priv(node_p)->cache_lock);
}

static inline 
void update_to_cache(MycoNode_t *node_p, const void *data_p, int size)
{
    Rwlock_wrlock(&node_priv(node_p)->cache_lock);
    memcpy(node_priv(node_p)->cache_p, data_p, size);
    Rwlock_wrunlock(&node_priv(node_p)->cache_lock);
}
#endif

static int SendEvent(struct DataNode* node_p, EventParam_t* param)
{
#if MN_NODE_COMM_FLOW_TRACE_ENABLE
    static const char *_event_str[] = {
        "EVENT_NONE",
        "EVENT_PUBLISH",
        "EVENT_PULL",
        "EVENT_NOTIFY",
        "EVENT_PUBLISH_SIG",
    };

    MN_NODE_COMM_FLOW_TRACE(
        "Comm Event Flow: sender=%s --<event:%s>--> recver=%s, size=%d", 
        param->sender->name, 
        _event_str[param->event], 
        param->recver->name, 
        param->size
    );

#endif

#if MN_CACHE_SUPPORT_ENABLE
    // check if CONF_CACHED is enabled
    if (param->event == EVENT_PULL) {
        struct DataNodePriv * const priv = node_priv(node_p);

        if ((node_p->conflags & CONF_CACHED) && priv->cache_p) {
            copy_from_cache(node_p, param->data_p, param->size);
            return MN_OK;
        }
    }
#endif

    if (!node_p->event_cb) return MN_ERR_FAIL;
    return node_p->event_cb(node_p, param);
}

static int node_publish(MycoNode_t *node_p, const void *data_p, int size, int just_signal)
{
    struct DataNodePriv * const priv = node_priv(node_p);

#if MN_CACHE_SUPPORT_ENABLE
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
        MycoNode_t* sub_node = sub_ll_node->data;
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

    return MN_OK;
}

MN_API int MycoNet_NodePublish(MycoNode_t *node_p, const void *data_p, int size)
{
    if (!node_p || !data_p || size < 0) return MN_ERR_INVALID;
    int err = MN_OK;
    if ((err = check_hub_and_node_work(node_p)) != MN_OK) return err;

    if (node_p->size != 0 && node_p->size != (uint32_t)size) 
        return MN_ERR_SIZE_MISMATCH;

    return node_publish(node_p, data_p, size, 0); // publish with data
}

MN_API int MycoNet_NodePublishSignal(MycoNode_t *node_p, const void *data_p, int size)
{
    if (!node_p || !data_p || size < 0) return MN_ERR_INVALID;
    int err = MN_OK;
    if ((err = check_hub_and_node_work(node_p)) != MN_OK) return err;
    if (node_p->size != 0 && node_p->size != (uint32_t)size) 
        return MN_ERR_SIZE_MISMATCH;

    return node_publish(node_p, data_p, size, 1); // publish just signal
}

MN_API int MycoNet_NodePull(MycoNode_t *node_p, const char *name, void *data_p, uint32_t size) 
{
    if (!node_p || !name || !data_p) return MN_ERR_INVALID;
    int err = MN_OK;
    if ((err = check_hub_and_node_work(node_p)) != MN_OK) return err;

    MycoNode_t *pub_node = MycoNet_SearchNode(name);
    if (!pub_node) return MN_ERR_NOTFOUND;
    if (pub_node->size != size) return MN_ERR_SIZE_MISMATCH;

    // check if the target node supports the PULL event
    if (!(pub_node->event_msk & EVENT_PULL)) {
        return MN_ERR_NOSUPPORT;
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

MN_API int MycoNet_NodeNotify(MycoNode_t *node_p, const char *name, const void *data_p, int size) 
{
    if (!node_p || !name || !data_p || size < 0) return MN_ERR_INVALID;
    int err = MN_OK;
    if ((err = check_hub_and_node_work(node_p)) != MN_OK) return err;

    MycoNode_t *target_node = MycoNet_SearchNode(name);
    if (!target_node) return MN_ERR_NOTFOUND;

#if MN_RESTRICT_NOTIFY_SIZE_CHECK_ENABLE
    if (size != target_node->notify_size) return MN_ERR_SIZE_MISMATCH;
#endif

    // check if the target node supports the NOTIFY event
    if (!(target_node->event_msk & EVENT_NOTIFY)) {
        return MN_ERR_NOSUPPORT;
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
