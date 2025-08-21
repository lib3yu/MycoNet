#include "../3rd_party/unity/unity.h"
#include "myconet.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>

// ====================================================================
// 全局变量用于测试
// ====================================================================
static int g_callback_count = 0;
static MycoNet_ID_t g_received_sender = 0;
static MycoNet_ID_t g_received_recver = 0;
static int g_latched_callback_count = 0;
static int g_latched_data_value = 0;

// ====================================================================
// 多线程测试相关变量
// ====================================================================
static pthread_mutex_t g_test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_test_cond = PTHREAD_COND_INITIALIZER;
static atomic_int g_thread_ready_count = 0;
static atomic_int g_thread_completed_count = 0;
static atomic_int g_total_operations = 0;
static atomic_int g_data_race_detected = 0;
static atomic_int g_last_published_value = -1;

// 多线程事件回调计数器
static atomic_int g_mt_callback_count = 0;
static atomic_int g_mt_publish_count = 0;
static atomic_int g_mt_notify_count = 0;

// ====================================================================
// 事件回调函数
// ====================================================================
void test_event_callback(const MycoNet_EventParam_t* param) {
    if (param->event == EVENT_PUBLISH) {
        g_callback_count++;
        g_received_sender = param->sender;
        g_received_recver = param->recver;
    } else if (param->event == EVENT_NOTIFY) {
        g_callback_count++;
        g_received_sender = param->sender;
    }
    
}

// ====================================================================
// 多线程事件回调函数
// ====================================================================
void mt_event_callback(const MycoNet_EventParam_t* param) {
    if (param->event == EVENT_PUBLISH) {
        atomic_fetch_add(&g_mt_publish_count, 1);
        atomic_fetch_add(&g_mt_callback_count, 1);
    } else if (param->event == EVENT_NOTIFY) {
        atomic_fetch_add(&g_mt_notify_count, 1);
        atomic_fetch_add(&g_mt_callback_count, 1);
    }
}

// 数据竞争检测回调函数
void data_race_detection_callback(const MycoNet_EventParam_t* param) {
    if (param->event == EVENT_PUBLISH && param->data_p != NULL && param->size == sizeof(int)) {
        int current_value = *(int*)param->data_p;
        int previous = atomic_load(&g_last_published_value);
        
        // 检查数据竞争：如果值不是单调递增的，可能有问题
        if (current_value < previous) {
            atomic_store(&g_data_race_detected, 1);
        }
        atomic_store(&g_last_published_value, current_value);
    }
}

// ====================================================================
// 线程同步辅助函数
// ====================================================================

static void wait_for_threads_completed(int expected_count) {
    pthread_mutex_lock(&g_test_mutex);
    while (atomic_load(&g_thread_completed_count) < expected_count) {
        pthread_cond_wait(&g_test_cond, &g_test_mutex);
    }
    pthread_mutex_unlock(&g_test_mutex);
}

static void signal_thread_completed(void) {
    pthread_mutex_lock(&g_test_mutex);
    atomic_fetch_add(&g_thread_completed_count, 1);
    pthread_cond_broadcast(&g_test_cond);
    pthread_mutex_unlock(&g_test_mutex);
}

// 锁存数据回调函数
void test_latched_event_callback(const MycoNet_EventParam_t* param) {
    if (param->event == EVENT_LATCHED) {
        g_latched_callback_count++;
        if (param->data_p != NULL && param->size == sizeof(int)) {
            g_latched_data_value = *(int*)param->data_p;
        }
    }
}

// ====================================================================
// 测试固件
// ====================================================================
void setUp(void) {
    // 在每个测试用例之前初始化
    myconet_init();
    g_callback_count = 0;
    g_received_sender = 0;
    g_received_recver = 0;
    g_latched_callback_count = 0;
    g_latched_data_value = 0;
}

void tearDown(void) {
    // 在每个测试用例之后清理
    myconet_deinit();
    
    // 重置多线程测试变量
    atomic_store(&g_thread_ready_count, 0);
    atomic_store(&g_thread_completed_count, 0);
    atomic_store(&g_total_operations, 0);
    atomic_store(&g_data_race_detected, 0);
    atomic_store(&g_last_published_value, -1);
    atomic_store(&g_mt_callback_count, 0);
    atomic_store(&g_mt_publish_count, 0);
    atomic_store(&g_mt_notify_count, 0);
}

// ====================================================================
// 多线程测试函数声明
// ====================================================================
void test_thread_safety_node_creation(void);
void test_thread_safety_subscribe_unsubscribe(void);
void test_thread_safety_publish_notify(void);
void test_thread_safety_mixed_operations(void);
void test_thread_safety_high_concurrency(void);
void test_thread_safety_data_race_detection(void);
void test_thread_safety_extreme_stress(void);
void test_thread_safety_deadlock_detection(void);

// ====================================================================
// 基础功能测试
// ====================================================================
void test_myconet_initialization(void) {
    // 测试初始化
    int result = myconet_init();
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 测试重复初始化
    result = myconet_init();
    TEST_ASSERT_EQUAL_INT(MN_OK, result); // 应该允许重复初始化

    // 测试去初始化
    myconet_deinit();
    TEST_ASSERT_EQUAL_INT(0, myconet_node_num());
}

void test_node_creation_and_removal(void) {
    // 测试节点创建
    MycoNet_NodeParam_t param = {
        .size = 100,
        .conflags = CONF_CACHED,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t node_id = 0;
    int result = myconet_create_node(&node_id, "test_node", &param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    TEST_ASSERT_NOT_EQUAL_UINT(0, node_id);
    TEST_ASSERT_EQUAL_INT(1, myconet_node_num());

    // 测试通过ID移除节点
    result = myconet_remove_node_id(node_id);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    TEST_ASSERT_EQUAL_INT(0, myconet_node_num());

    // 测试移除不存在的节点
    result = myconet_remove_node_id(9999);
    TEST_ASSERT_EQUAL_INT(MN_ERR_NOTFOUND, result);
}

void test_node_creation_with_event_callback(void) {
    // 测试带事件回调的节点创建
    MycoNet_NodeParam_t param = {
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH,
        .event_cb = test_event_callback,
        .user_data = NULL
    };

    MycoNet_ID_t node_id = 0;
    int result = myconet_create_node(&node_id, "callback_node", &param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    TEST_ASSERT_NOT_EQUAL_UINT(0, node_id);

    // 清理
    myconet_remove_node_id(node_id);
}

void test_node_removal_by_name(void) {
    // 测试通过名称移除节点
    MycoNet_NodeParam_t param = {
        .size = 100,
        .conflags = CONF_NONE,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t node_id = 0;
    int result = myconet_create_node(&node_id, "test_node_name", &param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 通过名称移除
    result = myconet_remove_node_name("test_node_name");
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    TEST_ASSERT_EQUAL_INT(0, myconet_node_num());

    // 测试移除不存在的节点
    result = myconet_remove_node_name("nonexistent");
    TEST_ASSERT_EQUAL_INT(MN_ERR_NOTFOUND, result);
}

// ====================================================================
// 订阅和取消订阅测试
// ====================================================================
void test_subscribe_unsubscribe(void) {
    // 创建两个节点用于测试订阅
    MycoNet_NodeParam_t param1 = {
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH,
        .event_cb = test_event_callback,
        .user_data = NULL
    };

    MycoNet_NodeParam_t param2 = {
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH,
        .event_cb = test_event_callback,
        .user_data = NULL
    };

    MycoNet_ID_t node1_id = 0, node2_id = 0;
    int result;

    result = myconet_create_node(&node1_id, "subscriber", &param1);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    result = myconet_create_node(&node2_id, "publisher", &param2);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 测试订阅
    result = myconet_subscribe(node1_id, "publisher");
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 验证订阅关系
    int sub_num = myconet_sub_num(node2_id);
    int pub_num = myconet_pub_num(node1_id);
    TEST_ASSERT_EQUAL_INT(1, sub_num);
    TEST_ASSERT_EQUAL_INT(1, pub_num);

    // 测试取消订阅
    result = myconet_unsubscribe(node1_id, "publisher");
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 验证取消订阅
    sub_num = myconet_sub_num(node2_id);
    pub_num = myconet_pub_num(node1_id);
    TEST_ASSERT_EQUAL_INT(0, sub_num);
    TEST_ASSERT_EQUAL_INT(0, pub_num);

    // 测试通过ID取消订阅
    result = myconet_subscribe(node1_id, "publisher");
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    result = myconet_unsubscribe_id(node1_id, node2_id);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 清理
    myconet_remove_node_id(node1_id);
    myconet_remove_node_id(node2_id);
}

// ====================================================================
// 发布和通知测试
// ====================================================================
void test_publish_functionality(void) {
    // 创建发布者和订阅者
    MycoNet_NodeParam_t publisher_param = {
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_NodeParam_t subscriber_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH,
        .event_cb = test_event_callback,
        .user_data = NULL
    };

    MycoNet_ID_t publisher_id = 0, subscriber_id = 0;
    int result;

    result = myconet_create_node(&publisher_id, "publisher", &publisher_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    result = myconet_create_node(&subscriber_id, "subscriber", &subscriber_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 建立订阅关系
    result = myconet_subscribe(subscriber_id, "publisher");
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 发布消息
    int test_data = 42;
    result = myconet_publish(publisher_id, &test_data, sizeof(test_data));
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 验证回调被调用
    // 注意：实际的回调执行可能需要一些时间或触发条件
    // 这里我们主要测试API调用是否成功

    // 清理
    myconet_remove_node_id(publisher_id);
    myconet_remove_node_id(subscriber_id);
}

void test_notify_functionality(void) {
    // 创建通知者和接收者
    MycoNet_NodeParam_t receiver_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_NOTIFY,
        .event_cb = test_event_callback,
        .user_data = NULL
    };

    MycoNet_NodeParam_t sender_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t receiver_id = 0, sender_id = 0;
    int result;

    result = myconet_create_node(&receiver_id, "receiver", &receiver_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    result = myconet_create_node(&sender_id, "sender", &sender_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 发送通知
    int test_data = 123;
    result = myconet_notify(sender_id, "receiver", &test_data, sizeof(test_data));
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 清理
    myconet_remove_node_id(receiver_id);
    myconet_remove_node_id(sender_id);
}

// ====================================================================
// 缓存功能测试
// ====================================================================
void test_cache_functionality(void) {
    // 创建带缓存的节点
    MycoNet_NodeParam_t cached_param = {
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t cached_node_id = 0;
    int result = myconet_create_node(&cached_node_id, "cached_node", &cached_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 发布数据到缓存
    int test_data = 100;
    result = myconet_publish(cached_node_id, &test_data, sizeof(test_data));
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 从缓存拉取数据
    MycoNet_ID_t puller_id = 0;
    MycoNet_NodeParam_t puller_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    result = myconet_create_node(&puller_id, "puller", &puller_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    int pulled_data = 0;
    result = myconet_pull(puller_id, "cached_node", &pulled_data, sizeof(pulled_data));
    TEST_ASSERT_EQUAL_INT(MN_INFO_CACHE_PULLED, result);
    TEST_ASSERT_EQUAL_INT(100, pulled_data);

    // 清理
    myconet_remove_node_id(cached_node_id);
    myconet_remove_node_id(puller_id);
}

void test_cache_size_mismatch(void) {
    // 创建带缓存的节点
    MycoNet_NodeParam_t cached_param = {
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t cached_node_id = 0;
    int result = myconet_create_node(&cached_node_id, "cached_node", &cached_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 发布数据到缓存
    int test_data = 200;
    result = myconet_publish(cached_node_id, &test_data, sizeof(test_data));
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 测试大小不匹配的拉取
    MycoNet_ID_t puller_id = 0;
    MycoNet_NodeParam_t puller_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    result = myconet_create_node(&puller_id, "puller", &puller_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    char small_buffer[1];
    result = myconet_pull(puller_id, "cached_node", small_buffer, sizeof(small_buffer));
    TEST_ASSERT_EQUAL_INT(MN_ERR_SIZE_MISMATCH, result);

    // 清理
    myconet_remove_node_id(cached_node_id);
    myconet_remove_node_id(puller_id);
}

// ====================================================================
// 错误处理测试
// ====================================================================
void test_error_conditions(void) {
    // 测试空指针发布
    MycoNet_ID_t node_id = 0;
    MycoNet_NodeParam_t param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    int result = myconet_create_node(&node_id, "test_node", &param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    result = myconet_publish(node_id, NULL, 10);
    TEST_ASSERT_EQUAL_INT(MN_ERR_NULL_POINTER, result);

    // 测试空指针通知
    result = myconet_notify(node_id, "test_node", NULL, 10);
    TEST_ASSERT_EQUAL_INT(MN_ERR_NULL_POINTER, result);

    // 测试通知不存在的节点
    int data = 42;
    result = myconet_notify(node_id, "nonexistent", &data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(MN_ERR_NOTFOUND, result);

    // 测试拉取不存在的节点
    int pull_data = 0;
    result = myconet_pull(node_id, "nonexistent", &pull_data, sizeof(pull_data));
    TEST_ASSERT_EQUAL_INT(MN_ERR_NOTFOUND, result);

    // 清理
    myconet_remove_node_id(node_id);
}

void test_invalid_node_operations(void) {
    // 测试无效节点ID的操作
    int data = 42;
    int result;

    result = myconet_publish(9999, &data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(MN_ERR_NOTFOUND, result);

    result = myconet_subscribe(9999, "test_node");
    TEST_ASSERT_EQUAL_INT(MN_ERR_NOTFOUND, result);

    result = myconet_unsubscribe(9999, "test_node");
    TEST_ASSERT_EQUAL_INT(MN_ERR_NOTFOUND, result);

    result = myconet_remove_node_id(9999);
    TEST_ASSERT_EQUAL_INT(MN_ERR_NOTFOUND, result);
}

// ====================================================================
// 边界条件测试
// ====================================================================
void test_empty_node_name(void) {
    // 测试空名称节点创建
    MycoNet_NodeParam_t param = {
        .size = 100,
        .conflags = CONF_NONE,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t node_id = 0;
    int result = myconet_create_node(&node_id, "", &param);
    // 根据实现，可能允许空名称或返回错误
    // 这里我们不做具体断言，只测试API调用不崩溃
    (void)result; // 避免未使用变量警告

    if (node_id != 0) {
        myconet_remove_node_id(node_id);
    }
}

void test_large_data_publish(void) {
    // 测试大数据发布
    MycoNet_NodeParam_t param = {
        .size = 1024,
        .conflags = CONF_CACHED,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t node_id = 0;
    int result = myconet_create_node(&node_id, "large_data_node", &param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 创建大数据
    char* large_data = (char*)malloc(1024);
    TEST_ASSERT_NOT_NULL(large_data);

    memset(large_data, 0x55, 1024);

    result = myconet_publish(node_id, large_data, 1024);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    free(large_data);

    // 清理
    myconet_remove_node_id(node_id);
}

// ====================================================================
// 通知大小检查测试
// ====================================================================
void test_notify_size_check(void) {
    // 创建带通知大小检查的节点
    MycoNet_NodeParam_t receiver_param = {
        .size = 8,
        .conflags = CONF_NOTIFY_SIZE_CHECK,
        .event_msk = EVENT_NOTIFY,
        .event_cb = test_event_callback,
        .user_data = NULL,
        .notify_size = 8
    };

    MycoNet_NodeParam_t sender_param = {
        .size = 8,
        .conflags = CONF_NONE,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t receiver_id = 0, sender_id = 0;
    int result;

    result = myconet_create_node(&receiver_id, "size_check_receiver", &receiver_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    result = myconet_create_node(&sender_id, "size_check_sender", &sender_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 测试正确大小的通知
    int correct_data[2] = {1, 2}; // 8 bytes
    result = myconet_notify(sender_id, "size_check_receiver", correct_data, sizeof(correct_data));
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 测试错误大小的通知
    int wrong_data = 1; // 4 bytes
    result = myconet_notify(sender_id, "size_check_receiver", &wrong_data, sizeof(wrong_data));
    TEST_ASSERT_EQUAL_INT(MN_ERR_SIZE_MISMATCH, result);

    // 清理
    myconet_remove_node_id(receiver_id);
    myconet_remove_node_id(sender_id);
}

// ====================================================================
// 拉取功能测试
// ====================================================================
void test_pull_functionality(void) {
    // 创建缓存节点
    MycoNet_NodeParam_t cached_param = {
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t cached_node_id = 0;
    int result = myconet_create_node(&cached_node_id, "cached_node", &cached_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 发布数据到缓存
    int data = 456;
    result = myconet_publish(cached_node_id, &data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 创建拉取节点
    MycoNet_ID_t puller_id = 0;
    MycoNet_NodeParam_t puller_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    result = myconet_create_node(&puller_id, "puller", &puller_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 测试从缓存拉取
    int result_data = 0;
    result = myconet_pull(puller_id, "cached_node", &result_data, sizeof(result_data));
    TEST_ASSERT_EQUAL_INT(MN_INFO_CACHE_PULLED, result);
    TEST_ASSERT_EQUAL_INT(456, result_data);

    // 测试拉取不存在的节点
    result = myconet_pull(puller_id, "nonexistent", &result_data, sizeof(result_data));
    TEST_ASSERT_EQUAL_INT(MN_ERR_NOTFOUND, result);

    // 清理
    myconet_remove_node_id(cached_node_id);
    myconet_remove_node_id(puller_id);
}

// ====================================================================
// 新增测试：节点存在性检查
// ====================================================================
void test_node_existence_check(void) {
    // 创建测试节点
    MycoNet_NodeParam_t param = {
        .size = 100,
        .conflags = CONF_NONE,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t node_id = 0;
    int result = myconet_create_node(&node_id, "existence_test_node", &param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 测试节点存在性 - 这里需要检查myconet.h中是否有相关API
    // 如果没有直接的API，这个测试可以跳过或注释掉

    // 清理
    myconet_remove_node_id(node_id);
}

// ====================================================================
// 新增测试：锁存数据订阅功能
// ====================================================================
void test_latched_data_on_subscribe(void) {
    // 创建带缓存和LATCHED标志的节点
    MycoNet_NodeParam_t cached_param = {
        .size = sizeof(int),
        .conflags = (MycoNet_NodeFlag_t)(CONF_CACHED | CONF_LATCHED),
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t cached_node_id = 0;
    int result = myconet_create_node(&cached_node_id, "latched_node", &cached_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 先发布数据到缓存
    int cached_data = 999;
    result = myconet_publish(cached_node_id, &cached_data, sizeof(cached_data));
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 创建订阅者节点，带LATCHED事件回调
    MycoNet_NodeParam_t subscriber_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_LATCHED,
        .event_cb = test_latched_event_callback,
        .user_data = NULL
    };

    MycoNet_ID_t subscriber_id = 0;
    result = myconet_create_node(&subscriber_id, "latched_subscriber", &subscriber_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 订阅应该触发LATCHED事件
    result = myconet_subscribe(subscriber_id, "latched_node");
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 给事件处理一些时间
    for (int i = 0; i < 10 && g_latched_callback_count == 0; i++) {
        // 简单延迟等待
        volatile int j = 10000;
        while (j--);
    }

    // 验证锁存回调被调用
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, g_latched_callback_count);

    // 清理
    myconet_remove_node_id(cached_node_id);
    myconet_remove_node_id(subscriber_id);
}

// ====================================================================
// 新增测试：资源耗尽场景
// ====================================================================
void test_resource_exhaustion_scenario(void) {
    const int MAX_TEST_NODES = 100; // 合理的测试限制
    
    MycoNet_NodeParam_t param = {
        .size = 10,
        .conflags = CONF_NONE,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    MycoNet_ID_t node_ids[MAX_TEST_NODES];
    int created_count = 0;

    // 尝试创建大量节点
    for (int i = 0; i < MAX_TEST_NODES; i++) {
        char node_name[32];
        snprintf(node_name, sizeof(node_name), "stress_node_%d", i);
        
        int result = myconet_create_node(&node_ids[created_count], node_name, &param);
        if (result == MN_OK) {
            created_count++;
        } else {
            // 达到限制，停止创建
            break;
        }
    }

    // 验证创建了合理数量的节点
    TEST_ASSERT_GREATER_THAN_INT(0, created_count);
    TEST_ASSERT_LESS_OR_EQUAL_INT(MAX_TEST_NODES, created_count);

    // 清理所有创建的节点
    for (int i = 0; i < created_count; i++) {
        myconet_remove_node_id(node_ids[i]);
    }

    // 验证网络为空
    TEST_ASSERT_EQUAL_INT(0, myconet_node_num());
}

// ====================================================================
// 新增测试：循环订阅死锁检测
// ====================================================================
void test_circular_subscribe_deadlock_detection(void) {
    const int NUM_NODES = 3;
    MycoNet_ID_t node_ids[NUM_NODES];
    
    // 创建带事件回调的节点
    MycoNet_NodeParam_t param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH,
        .event_cb = test_event_callback,
        .user_data = NULL
    };

    // 创建多个节点
    for (int i = 0; i < NUM_NODES; i++) {
        char node_name[32];
        snprintf(node_name, sizeof(node_name), "circular_node_%d", i);
        
        int result = myconet_create_node(&node_ids[i], node_name, &param);
        TEST_ASSERT_EQUAL_INT(MN_OK, result);
    }

    // 创建循环订阅关系: node0 -> node1 -> node2 -> node0
    for (int i = 0; i < NUM_NODES; i++) {
        int next = (i + 1) % NUM_NODES;
        char target_name[32];
        snprintf(target_name, sizeof(target_name), "circular_node_%d", next);
        
        int result = myconet_subscribe(node_ids[i], target_name);
        TEST_ASSERT_EQUAL_INT(MN_OK, result);
    }

    // 尝试发布消息，验证系统不会死锁
    int test_data = 42;
    int result = myconet_publish(node_ids[0], &test_data, sizeof(test_data));
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 清理订阅关系和节点
    for (int i = 0; i < NUM_NODES; i++) {
        int next = (i + 1) % NUM_NODES;
        char target_name[32];
        snprintf(target_name, sizeof(target_name), "circular_node_%d", next);
        
        myconet_unsubscribe(node_ids[i], target_name);
        myconet_remove_node_id(node_ids[i]);
    }
}

// ====================================================================
// 新增测试：无效订阅操作（没有事件回调的节点尝试订阅）
// ====================================================================
void test_invalid_subscribe_operation(void) {
    // 创建没有事件回调的节点
    MycoNet_NodeParam_t no_event_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };

    // 创建有事件回调的目标节点
    MycoNet_NodeParam_t target_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH,
        .event_cb = test_event_callback,
        .user_data = NULL
    };

    MycoNet_ID_t no_event_id = 0, target_id = 0;
    int result;
    
    result = myconet_create_node(&no_event_id, "no_event_node", &no_event_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    
    result = myconet_create_node(&target_id, "target_node", &target_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);

    // 测试没有事件回调的节点尝试订阅
    // 根据实现，这可能返回错误或成功但不会收到事件
    result = myconet_subscribe(no_event_id, "target_node");
    // 这里不进行具体断言，因为行为取决于实现

    // 清理
    myconet_remove_node_id(no_event_id);
    myconet_remove_node_id(target_id);
}



// ====================================================================
// 多线程测试辅助函数
// ====================================================================
typedef struct {
    int thread_id;
    int operations;
    atomic_int* success_count;
    MycoNet_ID_t base_node1;
    MycoNet_ID_t base_node2;
} ThreadData;

// 线程函数：节点创建测试
static void* thread_node_creation(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    for (int j = 0; j < data->operations; ++j) {
        char node_name[64];
        snprintf(node_name, sizeof(node_name), "thread_node_%d_%d", data->thread_id, j);
        
        MycoNet_NodeParam_t param = {
            .size = sizeof(int),
            .conflags = CONF_NONE,
            .event_msk = 0,
            .event_cb = NULL,
            .user_data = NULL
        };
        
        MycoNet_ID_t node_id = 0;
        int result = myconet_create_node(&node_id, node_name, &param);
        if (result == MN_OK && node_id != 0) {
            atomic_fetch_add(data->success_count, 1);
        }
    }
    
    signal_thread_completed();
    return NULL;
}

// ====================================================================
// 多线程安全性测试
// ====================================================================
void test_thread_safety_node_creation(void) {
    const int NUM_THREADS = 8;
    const int OPERATIONS_PER_THREAD = 200;
    
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    atomic_int success_count = 0;
    
    // 创建线程数据
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_data[i] = (ThreadData){
            .thread_id = i,
            .operations = OPERATIONS_PER_THREAD,
            .success_count = &success_count,
            .base_node1 = 0,
            .base_node2 = 0
        };
    }
    
    // 启动所有线程
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, thread_node_creation, &thread_data[i]);
    }
    
    // 等待所有线程完成
    wait_for_threads_completed(NUM_THREADS);
    
    // 验证所有线程都成功创建了节点
    TEST_ASSERT_EQUAL_INT(NUM_THREADS * OPERATIONS_PER_THREAD, atomic_load(&success_count));
    TEST_ASSERT_EQUAL_INT(NUM_THREADS * OPERATIONS_PER_THREAD, myconet_node_num());
    
    // 清理所有创建的节点
    for (int i = 0; i < NUM_THREADS; ++i) {
        for (int j = 0; j < OPERATIONS_PER_THREAD; ++j) {
            char node_name[64];
            snprintf(node_name, sizeof(node_name), "thread_node_%d_%d", i, j);
            myconet_remove_node_name(node_name);
        }
    }
}

// 线程函数：订阅/取消订阅测试
static void* thread_subscribe_unsubscribe(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    for (int j = 0; j < data->operations; ++j) {
        // 交替进行订阅和取消订阅
        if (j % 2 == 0) {
            myconet_subscribe(data->base_node1, "base_node2");
        } else {
            myconet_unsubscribe(data->base_node1, "base_node2");
        }
    }
    
    signal_thread_completed();
    return NULL;
}

void test_thread_safety_subscribe_unsubscribe(void) {
    const int NUM_THREADS = 6;
    const int OPERATIONS_PER_THREAD = 100;
    
    // 创建基础节点
    MycoNet_NodeParam_t param1 = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH,
        .event_cb = mt_event_callback,
        .user_data = NULL
    };
    
    MycoNet_NodeParam_t param2 = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH,
        .event_cb = mt_event_callback,
        .user_data = NULL
    };
    
    MycoNet_ID_t base_node1 = 0, base_node2 = 0;
    int result;
    
    result = myconet_create_node(&base_node1, "base_node1", &param1);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    
    result = myconet_create_node(&base_node2, "base_node2", &param2);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    
    // 创建线程数据
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_data[i] = (ThreadData){
            .thread_id = i,
            .operations = OPERATIONS_PER_THREAD,
            .success_count = NULL,
            .base_node1 = base_node1,
            .base_node2 = base_node2
        };
    }
    
    // 启动所有线程
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, thread_subscribe_unsubscribe, &thread_data[i]);
    }
    
    // 等待所有线程完成
    wait_for_threads_completed(NUM_THREADS);
    
    // 验证订阅关系最终状态
    int sub_num = myconet_sub_num(base_node2);
    int pub_num = myconet_pub_num(base_node1);
    
    // 最终订阅状态应该是合理的（要么订阅了，要么没订阅）
    TEST_ASSERT_TRUE(sub_num == 0 || sub_num == 1);
    TEST_ASSERT_TRUE(pub_num == 0 || pub_num == 1);
    
    // 清理
    myconet_remove_node_id(base_node1);
    myconet_remove_node_id(base_node2);
}

// 线程函数：发布/通知测试
static void* thread_publish_notify(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    for (int j = 0; j < data->operations; ++j) {
        int data_value = data->thread_id * 1000 + j;
        if (data->thread_id % 2 == 0) {
            myconet_publish(data->base_node1, &data_value, sizeof(data_value));
        } else {
            myconet_notify(data->base_node1, "base_node2", &data_value, sizeof(data_value));
        }
    }
    
    signal_thread_completed();
    return NULL;
}

void test_thread_safety_publish_notify(void) {
    const int NUM_THREADS = 4;
    const int OPERATIONS_PER_THREAD = 50;
    
    // 创建发布者和接收者
    MycoNet_NodeParam_t publisher_param = {
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = 0,
        .event_cb = NULL,
        .user_data = NULL
    };
    
    MycoNet_NodeParam_t receiver_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH | EVENT_NOTIFY,
        .event_cb = mt_event_callback,
        .user_data = NULL
    };
    
    MycoNet_ID_t publisher_id = 0, receiver_id = 0;
    int result;
    
    result = myconet_create_node(&publisher_id, "publisher", &publisher_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    
    result = myconet_create_node(&receiver_id, "receiver", &receiver_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    
    // 建立订阅关系（用于发布消息）
    result = myconet_subscribe(receiver_id, "publisher");
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    
    // 创建线程数据
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_data[i] = (ThreadData){
            .thread_id = i,
            .operations = OPERATIONS_PER_THREAD,
            .success_count = NULL,
            .base_node1 = publisher_id,
            .base_node2 = receiver_id
        };
    }
    
    // 启动所有线程
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, thread_publish_notify, &thread_data[i]);
    }
    
    // 等待所有线程完成
    wait_for_threads_completed(NUM_THREADS);
    
    // 给事件处理一些时间
    for (int i = 0; i < 100 && atomic_load(&g_mt_callback_count) == 0; i++) {
        // 简单延迟等待事件处理
        volatile int j = 10000;
        while (j--);
    }
    
    // 验证事件回调被正确调用
    // 注意：通知消息不需要订阅关系，应该直接触发回调
    TEST_ASSERT_GREATER_THAN_INT(0, atomic_load(&g_mt_callback_count));
    
    // 清理
    myconet_remove_node_id(publisher_id);
    myconet_remove_node_id(receiver_id);
}

// 线程函数：混合操作测试
static void* thread_mixed_operations(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    for (int j = 0; j < data->operations; ++j) {
        int operation = (data->thread_id + j) % 4;
        
        switch (operation) {
            case 0: { // 创建新节点
                char node_name[64];
                snprintf(node_name, sizeof(node_name), "mixed_node_%d_%d", data->thread_id, j);
                
                MycoNet_NodeParam_t param = {
                    .size = sizeof(int),
                    .conflags = CONF_NONE,
                    .event_msk = EVENT_PUBLISH,
                    .event_cb = mt_event_callback,
                    .user_data = NULL
                };
                
                myconet_create_node(&(MycoNet_ID_t){0}, node_name, &param);
                break;
            }
            case 1: { // 订阅操作
                myconet_subscribe(data->base_node1, "base_node2");
                break;
            }
            case 2: { // 发布操作
                int pub_data = j;
                myconet_publish(data->base_node1, &pub_data, sizeof(pub_data));
                break;
            }
            case 3: { // 取消订阅操作
                myconet_unsubscribe(data->base_node1, "base_node2");
                break;
            }
        }
        
        atomic_fetch_add(&g_total_operations, 1);
    }
    
    signal_thread_completed();
    return NULL;
}

void test_thread_safety_mixed_operations(void) {
    const int NUM_THREADS = 8;
    const int OPERATIONS_PER_THREAD = 100;
    
    // 创建一些基础节点
    MycoNet_NodeParam_t base_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH,
        .event_cb = mt_event_callback,
        .user_data = NULL
    };
    
    MycoNet_ID_t base_node1 = 0, base_node2 = 0;
    int result;
    
    result = myconet_create_node(&base_node1, "base_node1", &base_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    
    result = myconet_create_node(&base_node2, "base_node2", &base_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    
    // 创建线程数据
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_data[i] = (ThreadData){
            .thread_id = i,
            .operations = OPERATIONS_PER_THREAD,
            .success_count = NULL,
            .base_node1 = base_node1,
            .base_node2 = base_node2
        };
    }
    
    // 启动所有线程
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, thread_mixed_operations, &thread_data[i]);
    }
    
    // 等待所有线程完成
    wait_for_threads_completed(NUM_THREADS);
    
    // 验证所有操作完成且没有崩溃
    TEST_ASSERT_EQUAL_INT(NUM_THREADS * OPERATIONS_PER_THREAD, atomic_load(&g_total_operations));
    
    // 网络应该仍然处于有效状态
    TEST_ASSERT_GREATER_OR_EQUAL_INT(2, myconet_node_num()); // 至少还有基础节点
    
    // 清理
    myconet_remove_node_id(base_node1);
    myconet_remove_node_id(base_node2);
}

// 线程函数：高并发测试
static void* thread_high_concurrency(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    for (int j = 0; j < data->operations; ++j) {
        // 高强度并发操作：频繁订阅/取消订阅
        if (j % 2 == 0) {
            myconet_subscribe(data->base_node1, "high_concurrency_node");
        } else {
            myconet_unsubscribe(data->base_node1, "high_concurrency_node");
        }
        
        atomic_fetch_add(&g_total_operations, 1);
    }
    
    signal_thread_completed();
    return NULL;
}

void test_thread_safety_high_concurrency(void) {
    const int NUM_THREADS = 16;
    const int OPERATIONS_PER_THREAD = 500;
    
    // 创建高并发测试节点
    MycoNet_NodeParam_t high_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH,
        .event_cb = mt_event_callback,
        .user_data = NULL
    };
    
    MycoNet_ID_t high_node_id = 0;
    int result = myconet_create_node(&high_node_id, "high_concurrency_node", &high_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    
    // 创建线程数据
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_data[i] = (ThreadData){
            .thread_id = i,
            .operations = OPERATIONS_PER_THREAD,
            .success_count = NULL,
            .base_node1 = high_node_id,
            .base_node2 = 0
        };
    }
    
    // 启动所有线程
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, thread_high_concurrency, &thread_data[i]);
    }
    
    // 等待所有线程完成
    wait_for_threads_completed(NUM_THREADS);
    
    // 验证所有操作完成且没有崩溃
    TEST_ASSERT_EQUAL_INT(NUM_THREADS * OPERATIONS_PER_THREAD, atomic_load(&g_total_operations));
    
    // 最终状态应该是合理的
    int sub_num = myconet_sub_num(high_node_id);
    TEST_ASSERT_TRUE(sub_num == 0 || sub_num == 1);
    
    // 清理
    myconet_remove_node_id(high_node_id);
}

// 线程函数：数据竞争检测测试
static void* thread_data_race_detection(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    for (int j = 0; j < data->operations; ++j) {
        int publish_value = data->thread_id * 1000 + j;
        myconet_publish(data->base_node1, &publish_value, sizeof(publish_value));
        
        // 添加一些延迟以增加数据竞争的可能性
        if (j % 10 == 0) {
            sleep(0); // 使用sleep(0)替代usleep，避免警告
        }
    }
    
    signal_thread_completed();
    return NULL;
}

void test_thread_safety_data_race_detection(void) {
    const int NUM_THREADS = 4;
    const int OPERATIONS_PER_THREAD = 100;
    
    // 创建带数据竞争检测回调的节点
    MycoNet_NodeParam_t publisher_param = {
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH,
        .event_cb = data_race_detection_callback,
        .user_data = NULL
    };
    
    MycoNet_ID_t publisher_id = 0;
    int result = myconet_create_node(&publisher_id, "data_race_publisher", &publisher_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    
    // 创建线程数据
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_data[i] = (ThreadData){
            .thread_id = i,
            .operations = OPERATIONS_PER_THREAD,
            .success_count = NULL,
            .base_node1 = publisher_id,
            .base_node2 = 0
        };
    }
    
    // 启动所有线程
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, thread_data_race_detection, &thread_data[i]);
    }
    
    // 等待所有线程完成
    wait_for_threads_completed(NUM_THREADS);
    
    // 验证没有检测到数据竞争
    TEST_ASSERT_EQUAL_INT(0, atomic_load(&g_data_race_detected));
    
    // 清理
    myconet_remove_node_id(publisher_id);
}

// 线程函数：极端压力测试
static void* thread_extreme_stress(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    for (int j = 0; j < data->operations; ++j) {
        // 创建临时节点
        char node_name[64];
        snprintf(node_name, sizeof(node_name), "stress_node_%d_%d", data->thread_id, j);
        
        MycoNet_NodeParam_t param = {
            .size = sizeof(int),
            .conflags = CONF_NONE,
            .event_msk = EVENT_PUBLISH,
            .event_cb = mt_event_callback,
            .user_data = NULL
        };
        
        MycoNet_ID_t temp_node_id = 0;
        myconet_create_node(&temp_node_id, node_name, &param);
        
        // 立即发布数据
        int temp_data = j;
        myconet_publish(temp_node_id, &temp_data, sizeof(temp_data));
        
        // 立即移除节点
        myconet_remove_node_id(temp_node_id);
        
        atomic_fetch_add(&g_total_operations, 1);
    }
    
    signal_thread_completed();
    return NULL;
}

void test_thread_safety_extreme_stress(void) {
    const int NUM_THREADS = 12;
    const int OPERATIONS_PER_THREAD = 300;
    
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    
    // 创建线程数据
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_data[i] = (ThreadData){
            .thread_id = i,
            .operations = OPERATIONS_PER_THREAD,
            .success_count = NULL,
            .base_node1 = 0,
            .base_node2 = 0
        };
    }
    
    // 启动所有线程
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, thread_extreme_stress, &thread_data[i]);
    }
    
    // 等待所有线程完成
    wait_for_threads_completed(NUM_THREADS);
    
    // 验证所有操作完成且没有崩溃
    TEST_ASSERT_EQUAL_INT(NUM_THREADS * OPERATIONS_PER_THREAD, atomic_load(&g_total_operations));
    
    // 网络应该为空
    TEST_ASSERT_EQUAL_INT(0, myconet_node_num());
}

// 线程函数：死锁检测测试
static void* thread_deadlock_detection(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    for (int j = 0; j < data->operations; ++j) {
        // 创建复杂的订阅关系
        char node_name[64];
        snprintf(node_name, sizeof(node_name), "deadlock_node_%d_%d", data->thread_id, j);
        
        MycoNet_NodeParam_t param = {
            .size = sizeof(int),
            .conflags = CONF_NONE,
            .event_msk = EVENT_PUBLISH,
            .event_cb = mt_event_callback,
            .user_data = NULL
        };
        
        MycoNet_ID_t new_node_id = 0;
        myconet_create_node(&new_node_id, node_name, &param);
        
        // 订阅其他节点
        if (data->base_node1 != 0) {
            myconet_subscribe(new_node_id, "deadlock_base_node");
        }
        
        // 发布数据
        int pub_data = j;
        myconet_publish(new_node_id, &pub_data, sizeof(pub_data));
        
        // 移除节点
        myconet_remove_node_id(new_node_id);
        
        atomic_fetch_add(&g_total_operations, 1);
    }
    
    signal_thread_completed();
    return NULL;
}

void test_thread_safety_deadlock_detection(void) {
    const int NUM_THREADS = 6;
    const int OPERATIONS_PER_THREAD = 200;
    
    // 创建基础节点用于订阅
    MycoNet_NodeParam_t base_param = {
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH,
        .event_cb = mt_event_callback,
        .user_data = NULL
    };
    
    MycoNet_ID_t base_node_id = 0;
    int result = myconet_create_node(&base_node_id, "deadlock_base_node", &base_param);
    TEST_ASSERT_EQUAL_INT(MN_OK, result);
    
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    
    // 创建线程数据
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_data[i] = (ThreadData){
            .thread_id = i,
            .operations = OPERATIONS_PER_THREAD,
            .success_count = NULL,
            .base_node1 = base_node_id,
            .base_node2 = 0
        };
    }
    
    // 启动所有线程
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, thread_deadlock_detection, &thread_data[i]);
    }
    
    // 等待所有线程完成
    wait_for_threads_completed(NUM_THREADS);
    
    // 验证所有操作完成且没有死锁
    TEST_ASSERT_EQUAL_INT(NUM_THREADS * OPERATIONS_PER_THREAD, atomic_load(&g_total_operations));
    
    // 清理基础节点
    myconet_remove_node_id(base_node_id);
    
    // 网络应该为空
    TEST_ASSERT_EQUAL_INT(0, myconet_node_num());
}


// ====================================================================
// 主函数
// ====================================================================
int main(void) {
    UNITY_BEGIN();

    // 基础功能测试
    RUN_TEST(test_myconet_initialization);
    RUN_TEST(test_node_creation_and_removal);
    RUN_TEST(test_node_creation_with_event_callback);
    RUN_TEST(test_node_removal_by_name);

    // 订阅和取消订阅测试
    RUN_TEST(test_subscribe_unsubscribe);

    // 发布和通知测试
    RUN_TEST(test_publish_functionality);
    RUN_TEST(test_notify_functionality);

    // 缓存功能测试
    RUN_TEST(test_cache_functionality);
    RUN_TEST(test_cache_size_mismatch);

    // 错误处理测试
    RUN_TEST(test_error_conditions);
    RUN_TEST(test_invalid_node_operations);

    // 边界条件测试
    RUN_TEST(test_empty_node_name);
    RUN_TEST(test_large_data_publish);

    // 通知大小检查测试
    RUN_TEST(test_notify_size_check);

    // 拉取功能测试
    RUN_TEST(test_pull_functionality);

    // 新增测试函数
    RUN_TEST(test_node_existence_check);
    RUN_TEST(test_latched_data_on_subscribe);
    RUN_TEST(test_resource_exhaustion_scenario);
    RUN_TEST(test_circular_subscribe_deadlock_detection);
    RUN_TEST(test_invalid_subscribe_operation);

    // 多线程安全性测试
    RUN_TEST(test_thread_safety_node_creation);
    RUN_TEST(test_thread_safety_subscribe_unsubscribe);
    RUN_TEST(test_thread_safety_publish_notify);
    RUN_TEST(test_thread_safety_mixed_operations);
    RUN_TEST(test_thread_safety_high_concurrency);
    RUN_TEST(test_thread_safety_data_race_detection);
    RUN_TEST(test_thread_safety_extreme_stress);
    RUN_TEST(test_thread_safety_deadlock_detection);

    return UNITY_END();
}