#ifndef DATAHUB_H
#define DATAHUB_H

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

#define DH_NODE_NAME_MAX_LEN 64

// restrict notify size check to avoid buffer overflow
#define DH_RESTRICT_NOTIFY_SIZE_CHECK_ENABLE 1

// enable communication log
#define DH_NODE_COMMUNICATION_LOG_ENABLE 0
#if DH_NODE_COMMUNICATION_LOG_ENABLE
    #define DH_NODE_COMM_LOG(fmt, ...) printf(fmt "\n", __VA_ARGS__)
#else
    #define DH_NODE_COMM_LOG(fmt, ...) 
#endif

#define DH_API

//==============================================================================
// Error Codes
//==============================================================================

#define DH_OK                   (0)
#define DH_ERR_FAIL             (-1)
#define DH_ERR_TIMEOUT          (-2)
#define DH_ERR_NOMEM            (-3)
#define DH_ERR_NOTFOUND         (-4)
#define DH_ERR_NOSUPPORT        (-5)
#define DH_ERR_BUSY             (-6)
#define DH_ERR_INVALID          (-7)
#define DH_ERR_ACCESS           (-8)
#define DH_ERR_EXIST            (-9)
#define DH_ERR_NODATA           (-10)
#define DH_ERR_INITIALIZED      (-11)
#define DH_ERR_NOTINITIALIZED   (-12)
#define DH_ERR_SIZE_MISMATCH    (-13)
#define DH_ERR_NULL_POINTER     (-14)

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
#define DATAHUB_PRIV_DATA_SIZE (sizeof(DataNodePrivBase_t) * 52)
#define DataNodePrivSiz (DATAHUB_PRIV_DATA_SIZE / sizeof(DataNodePrivBase_t))


typedef struct DataNode {
    char               name[DH_NODE_NAME_MAX_LEN];
    uint32_t           size;
    NodeConf_t         conflags;
    EventMask_t        event_msk;
    EventCallback_t    event_cb;
    void*              user_data;
#if DH_RESTRICT_NOTIFY_SIZE_CHECK_ENABLE
    uint32_t           notify_size; 
#endif
    DataNodePrivBase_t priv[DataNodePrivSiz];
} DataNode_t;

/** 
 * only support pull/notify other nodes
 * can not publish/publish_signal to other nodes
 * can not subscribe to other nodes */
extern DataNode_t * const _dummyNode;

//==============================================================================
// API Functions
//==============================================================================

/**Hub API */
DH_API int DataHub_Init(void);
DH_API int DataHub_Deinit(void);
DH_API int DataHub_GetNodeNum(void);
DH_API DataNode_t *DataHub_SearchNode(const char *name);
DH_API const char *DataHub_GetErrStr(int err);

/**Node Tool API */
DH_API int DataHub_InitNode(DataNode_t *node_p);
DH_API int DataHub_DeinitNode(DataNode_t *node_p); 
DH_API int DataHub_GetNodePubNum(DataNode_t *node_p);
DH_API int DataHub_GetNodeSubNum(DataNode_t *node_p);
DH_API int DataHub_PushBackNode(DataNode_t *node_p);
DH_API int DataHub_RemoveNode(DataNode_t *node_p);
/**Node Communication API */
DH_API int DataHub_NodeSubscribe(DataNode_t *node_p, const char *name);
DH_API int DataHub_NodeUnsubscribe(DataNode_t *node_p, const char *name);
DH_API int DataHub_NodePublish(DataNode_t *node_p, const void *data_p, int size);
DH_API int DataHub_NodePublishSignal(DataNode_t *node_p, const void *data_p, int size);
DH_API int DataHub_NodePull(DataNode_t *node_p, const char *name, void *data_p, uint32_t size);
DH_API int DataHub_NodeNotify(DataNode_t *node_p, const char *name, const void *data_p, int size);

#ifdef __cplusplus
}
#endif

#endif // DATAHUB_H
