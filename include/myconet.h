#ifndef MYCONET_H
#define MYCONET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ====================================================================
// 1. 类型定义 (Type Definitions)
// ====================================================================
/**
 * @brief 节点在网络中的唯一标识符。
 */
typedef uint32_t MycoNet_ID_t;

/**
 * @brief 错误码定义。
 */
#define MN_INFO_CACHE_PULLED    (2)
#define MN_INFO_PENDING         (1)
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

#define MN_CONFIG_USE_STATIC_C 0 
#define MN_CONFIG_NODE_NAME_MAX_LEN 64
#define MN_CONFIG_NOTIFY_SIZE_CHECK 1
#define MN_CONFIG_

/**
 * @brief node flags
 */
typedef enum MycoNet_NodeConf {
    CONF_NONE   = 0,
    CONF_CACHED = 1 << 0,
    CONF_NOTIFY_SIZE_CHECK = 1 << 1,
    CONF_LATCHED = 1 << 2,
} MycoNet_NodeFlag_t;

/**
 * @brief event code
 */
typedef enum MycoNet_EventCode {
    EVENT_NONE        = 0,
    EVENT_PUBLISH     = 1 << 0,
    EVENT_PULL        = 1 << 1,
    EVENT_NOTIFY      = 1 << 2,
    EVENT_PUBLISH_SIG = 1 << 3,
    EVENT_LATCHED     = 1 << 4,
} MycoNet_EventCode_t;

/**
 * @brief 事件掩码，用于指定节点关心哪些事件。
 */
typedef uint8_t MycoNet_EventMask_t;

/**
 * @brief 事件回调函数接收的参数。
 */
typedef struct MycoNet_EventParam {
    MycoNet_EventCode_t event;
    MycoNet_ID_t sender;
    MycoNet_ID_t recver;
    void *data_p;
    uint32_t size;
} MycoNet_EventParam_t;

typedef struct MycoNet_SmallEventParam {
    MycoNet_EventCode_t event;
    MycoNet_ID_t sender;
    MycoNet_ID_t recver;
} MycoNet_SmallEventParam_t;

/**
 * @brief 事件回调函数指针类型。
 */
typedef void (*MycoNet_EventCb_t)(const MycoNet_EventParam_t *param);
typedef void (*MycoNet_SmallEventCb_t)(const MycoNet_SmallEventParam_t *param);

/**
 * @brief 创建节点时使用的配置结构体。
 */
typedef struct MycoNet_NodeParam {
    uint32_t size;
    MycoNet_NodeFlag_t conflags;
    MycoNet_EventMask_t event_msk;
    MycoNet_EventCb_t event_cb;
    void* user_data;
#if MN_CONFIG_NOTIFY_SIZE_CHECK
    uint32_t notify_size;
#endif
} MycoNet_NodeParam_t;


// ====================================================================
// 2. 函数接口 (Function Interfaces)
// ====================================================================
#define MN_API

MN_API int myconet_init();
MN_API void myconet_deinit();
MN_API int myconet_node_num();
MN_API const char *myconet_strerr(int err);
MN_API int myconet_create_node(MycoNet_ID_t *id, const char *name, const MycoNet_NodeParam_t *conf);
MN_API int myconet_remove_node_id(MycoNet_ID_t id);
MN_API int myconet_remove_node_name(const char *name);
MN_API int myconet_subscribe(MycoNet_ID_t id, const char *target_node_name);
MN_API int myconet_unsubscribe(MycoNet_ID_t id, const char *target_node_name);
MN_API int myconet_unsubscribe_id(MycoNet_ID_t id, MycoNet_ID_t target_node_id);
MN_API int myconet_publish(MycoNet_ID_t id, const void *data_p, size_t size);
// MN_API int myconet_publish_signal(MycoNet_ID_t id, const void *data_p, int size);
// MN_API int myconet_publish_signal_async(MycoNet_ID_t id, const void *data_p, int size);
MN_API int myconet_pull(MycoNet_ID_t id, const char *target_node_name, void *data_p, size_t size);
MN_API int myconet_pull_id(MycoNet_ID_t id, MycoNet_ID_t target_node_id, void *data_p, size_t size);
MN_API int myconet_notify(MycoNet_ID_t id, const char *target_node_name, const void *data_p, size_t size);
MN_API int myconet_notify_id(MycoNet_ID_t id, MycoNet_ID_t target_node_id, const void *data_p, size_t size);
MN_API int myconet_pub_num(MycoNet_ID_t id);
MN_API int myconet_sub_num(MycoNet_ID_t id);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // MYCONET_H
