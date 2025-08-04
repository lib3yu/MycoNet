#ifndef MYCONET_H
#define MYCONET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

//==============================================================================
// Configuration
//==============================================================================

#define NODE_NAME(xx) (#xx)

#define MN_NODE_NAME_MAX_LEN 64

// enable cache support
#define MN_CACHE_SUPPORT_ENABLE 1

// enable lock
#define MN_CONF_USE_LOCK 1


// restrict notify size check to avoid buffer overflow
#define MN_RESTRICT_NOTIFY_SIZE_CHECK_ENABLE 1

// enable communication log
#define MN_NODE_COMM_FLOW_TRACE_ENABLE 0
#if MN_NODE_COMM_FLOW_TRACE_ENABLE
    #define MN_NODE_COMM_FLOW_TRACE(fmt, ...) printf(fmt "\n", __VA_ARGS__)
#else
    #define MN_NODE_COMM_FLOW_TRACE(fmt, ...) 
#endif

#define MN_API

//==============================================================================
// Error Codes
//==============================================================================

#define MN_OK                   (0)
#define MN_ERR_FAIL             (-1)
#define MN_ERR_TIMEOUT          (-2)
#define MN_ERR_NOMEM            (-3)
#define MN_ERR_NOTFOUND         (-4)
#define MN_ERR_NOSUPPORT        (-5)
#define MN_ERR_BUSY             (-6)
#define MN_ERR_INVALID          (-7)
#define MN_ERR_ACCESS           (-8)
#define MN_ERR_EXIST            (-9)
#define MN_ERR_NODATA           (-10)
#define MN_ERR_INITIALIZED      (-11)
#define MN_ERR_NOTINITIALIZED   (-12)
#define MN_ERR_SIZE_MISMATCH    (-13)
#define MN_ERR_NULL_POINTER     (-14)

//==============================================================================
// Type Definitions
//==============================================================================

struct DataNode;

typedef enum NodeConf {
    CONF_NONE   = 0,
    CONF_CACHED = 1 << 0,
} NodeConf_t;

typedef enum EventCode {
    EVENT_NONE    = 0,
    EVENT_PUBLISH = 1 << 0,
    EVENT_PULL    = 1 << 1,
    EVENT_NOTIFY  = 1 << 2,
    EVENT_PUBLISH_SIG = 1 << 3,
} EventCode_t;

typedef uint8_t  EventMask_t;

typedef 
struct EventParam {
    EventCode_t event;
    struct DataNode *sender;
    struct DataNode *recver;
    void *data_p;
    uint32_t size;
} EventParam_t;


typedef int (*EventCallback_t)(struct DataNode* node_p, EventParam_t* param);


typedef uint32_t DataNodePrivBase_t;  // 32-bit aligned
#define MYCONET_PRIV_DATA_SIZE (sizeof(DataNodePrivBase_t) * 52)
#define DataNodePrivSiz (MYCONET_PRIV_DATA_SIZE / sizeof(DataNodePrivBase_t))


typedef struct DataNode {
    char               name[MN_NODE_NAME_MAX_LEN];
    uint32_t           size;
    NodeConf_t         conflags;
    EventMask_t        event_msk;
    EventCallback_t    event_cb;
    void*              user_data;
#if MN_RESTRICT_NOTIFY_SIZE_CHECK_ENABLE
    uint32_t           notify_size; 
#endif
    DataNodePrivBase_t priv[DataNodePrivSiz];
} MycoNode_t;

/** 
 * only support pull/notify other nodes
 * can not publish/publish_signal to other nodes
 * can not subscribe to other nodes */
extern MycoNode_t * const _dummyNode;

//==============================================================================
// API Functions
//==============================================================================

/**Hub API */
MN_API int MycoNet_Init(void);
MN_API int MycoNet_Deinit(void);
MN_API int MycoNet_GetNodeNum(void);
MN_API MycoNode_t *MycoNet_SearchNode(const char *name);
MN_API const char *MycoNet_GetErrStr(int err);
MN_API int MycoNet_PrintNodeList(int (*print_)(const char *fmt, ...));

/**Node Tool API */
MN_API int MycoNet_InitNode(MycoNode_t *node_p);
MN_API int MycoNet_DeinitNode(MycoNode_t *node_p); 
MN_API int MycoNet_GetNodePubNum(MycoNode_t *node_p);
MN_API int MycoNet_GetNodeSubNum(MycoNode_t *node_p);
MN_API int MycoNet_PushBackNode(MycoNode_t *node_p);
MN_API int MycoNet_RemoveNode(MycoNode_t *node_p);
/**Node Communication API */
MN_API int MycoNet_NodeSubscribe(MycoNode_t *node_p, const char *name);
MN_API int MycoNet_NodeUnsubscribe(MycoNode_t *node_p, const char *name);
MN_API int MycoNet_NodePublish(MycoNode_t *node_p, const void *data_p, int size);
MN_API int MycoNet_NodePublishSignal(MycoNode_t *node_p, const void *data_p, int size);
MN_API int MycoNet_NodePull(MycoNode_t *node_p, const char *name, void *data_p, uint32_t size);
MN_API int MycoNet_NodeNotify(MycoNode_t *node_p, const char *name, const void *data_p, int size);

#ifdef __cplusplus
}
#endif

#endif // MYCONET_H
