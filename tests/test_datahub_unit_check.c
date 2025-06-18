#include <check.h>
#include <stdlib.h>
#include "../src/DataHub.h"

// 测试回调函数
static int publish_called = 0;
static int pull_called = 0;
static int notify_called = 0;

static int test_callback(struct DataNode* node_p, EventParam_t* param) {
    switch(param->event) {
        case EVENT_PUBLISH: publish_called++; break;
        case EVENT_PULL: pull_called++; break;
        case EVENT_NOTIFY: notify_called++; break;
        default: break;
    }
    return DH_OK;
}

// 测试节点定义
DataNode_t node1_0 = {
    .name = "sensor_temp_1.0",
    .size = sizeof(float),
    .conflags = CONF_NONE,
    .event_msk = EVENT_PUBLISH | EVENT_PULL | EVENT_NOTIFY,
    .event_cb = test_callback
};

DataNode_t node1 = {
    .name = "sensor_temp_1.1",
    .size = sizeof(float),
    .conflags = CONF_CACHED,
    .event_msk = EVENT_PUBLISH | EVENT_PULL | EVENT_NOTIFY,
    .event_cb = test_callback
};

DataNode_t node2 = {
    .name = "display_unit",
    .size = sizeof(float),
    .conflags = CONF_NONE,
    .event_msk = EVENT_PUBLISH | EVENT_PULL | EVENT_NOTIFY,
    .event_cb = test_callback
};

// ================= Hub管理测试 =================
START_TEST(test_hub_init_deinit) {
    ck_assert_int_eq(DataHub_Init(), DH_OK);
    ck_assert_int_eq(DataHub_Deinit(), DH_OK);
    ck_assert_int_eq(DataHub_Deinit(), DH_ERR_NOTINITIALIZED); // 重复反初始化
}
END_TEST

START_TEST(test_node_count) {
    DataHub_Init();
    ck_assert_int_eq(DataHub_GetNodeNum(), 0);
    
    DataHub_InitNode(&node1);
    DataHub_PushBackNode(&node1);
    ck_assert_int_eq(DataHub_GetNodeNum(), 1);
    
    DataHub_Deinit();
}
END_TEST

// ================= 节点生命周期测试 =================
START_TEST(test_node_init_deinit) {
    ck_assert_int_eq(DataHub_InitNode(&node1), DH_OK);
    ck_assert_int_eq(DataHub_InitNode(&node1), DH_ERR_INITIALIZED); // 重复初始化
    
    ck_assert_int_eq(DataHub_DeinitNode(&node1), DH_OK);
    ck_assert_int_eq(DataHub_DeinitNode(&node1), DH_ERR_NOTINITIALIZED); // 重复反初始化
}
END_TEST

START_TEST(test_node_registration) {
    DataHub_Init();
    
    DataHub_InitNode(&node1);
    ck_assert_int_eq(DataHub_PushBackNode(&node1), DH_OK);
    ck_assert_int_eq(DataHub_PushBackNode(&node1), DH_ERR_EXIST); // 重复注册
    
    ck_assert_int_eq(DataHub_RemoveNode(&node1), DH_OK);
    ck_assert_int_eq(DataHub_RemoveNode(&node1), DH_ERR_NOTFOUND); // 重复移除
    
    DataHub_Deinit();
}
END_TEST

// ================= 订阅关系测试 =================
START_TEST(test_subscription) {
    DataHub_Init();
    DataHub_InitNode(&node1);
    DataHub_InitNode(&node2);
    DataHub_PushBackNode(&node1);
    DataHub_PushBackNode(&node2);
    
    ck_assert_int_eq(DataHub_NodeSubscribe(&node2, node1.name), DH_OK);
    ck_assert_int_eq(DataHub_GetNodePubNum(&node1), 1);
    ck_assert_int_eq(DataHub_GetNodeSubNum(&node2), 1);
    
    ck_assert_int_eq(DataHub_NodeUnsubscribe(&node2, node1.name), DH_OK);
    ck_assert_int_eq(DataHub_GetNodePubNum(&node1), 0);
    
    DataHub_Deinit();
}
END_TEST

// ================= 数据发布测试 =================
START_TEST(test_publish) {
    DataHub_Init();
    DataHub_InitNode(&node1);
    DataHub_InitNode(&node2);
    DataHub_PushBackNode(&node1);
    DataHub_PushBackNode(&node2);
    DataHub_NodeSubscribe(&node2, node1.name);
    
    float temp = 25.5f;
    publish_called = 0;
    ck_assert_int_eq(DataHub_NodePublish(&node1, &temp, sizeof(float)), DH_OK);
    ck_assert_int_eq(publish_called, 1); // 验证回调触发
    
    // 测试缓存功能
    float cached_value;
    ck_assert_int_eq(DataHub_NodePull(&node2, node1.name, &cached_value, sizeof(float)), DH_OK);
    ck_assert(cached_value == 25.5f);
    
    DataHub_Deinit();
}
END_TEST

// ================= 数据拉取测试 =================
START_TEST(test_pull) {
    DataHub_Init();
    DataHub_InitNode(&node1);
    DataHub_InitNode(&node1_0);
    DataHub_InitNode(&node2);
    DataHub_PushBackNode(&node1);
    DataHub_PushBackNode(&node1_0);
    DataHub_PushBackNode(&node2);
    
    float value;
    pull_called = 0;
    
    // 测试有缓存的节点 (node1)
    ck_assert_int_eq(DataHub_NodePull(&node2, node1.name, &value, sizeof(float)), DH_OK);
    ck_assert_int_eq(pull_called, 0);
    
    // 测试无缓存的节点 (node1_0)
    pull_called = 0;
    ck_assert_int_eq(DataHub_NodePull(&node2, node1_0.name, &value, sizeof(float)), DH_OK);
    ck_assert_int_eq(pull_called, 1);
    
    // 测试尺寸错误
    ck_assert_int_eq(DataHub_NodePull(&node2, node1.name, &value, 1), DH_ERR_SIZE_MISMATCH);
    
    // 边界测试
    ck_assert_int_eq(DataHub_NodePull(NULL, node1.name, &value, sizeof(float)), DH_ERR_INVALID);
    ck_assert_int_eq(DataHub_NodePull(&node2, NULL, &value, sizeof(float)), DH_ERR_INVALID);
    ck_assert_int_eq(DataHub_NodePull(&node2, "invalid_node", &value, sizeof(float)), DH_ERR_NOTFOUND);
    
    DataHub_Deinit();
}
END_TEST

// ================= 通知测试 =================
START_TEST(test_notify) {
    DataHub_Init();
    DataHub_InitNode(&node1);
    DataHub_InitNode(&node2);
    DataHub_PushBackNode(&node1);
    DataHub_PushBackNode(&node2);
    
    int command = 1;
    notify_called = 0;
    ck_assert_int_eq(DataHub_NodeNotify(&node2, node1.name, &command, sizeof(int)), DH_OK);
    ck_assert_int_eq(notify_called, 1);
    
    DataHub_Deinit();
}
END_TEST

// ================= 错误处理测试 =================
START_TEST(test_error_handling) {
    // 未初始化测试
    ck_assert_int_eq(DataHub_GetNodeNum(), DH_ERR_NOTINITIALIZED);
    ck_assert_ptr_eq(DataHub_SearchNode("test"), NULL);
    // 无效参数测试
    ck_assert_int_eq(DataHub_InitNode(NULL), DH_ERR_INVALID);
    ck_assert_int_eq(DataHub_NodePublish(NULL, NULL, 0), DH_ERR_INVALID);
    
    // 不支持的平台测试
    #if !defined(USE_FREERTOS) && !(defined(__GNUC__))
        ck_assert(0); // 应触发#error
    #endif
}
END_TEST

// ================= 测试套件配置 =================
Suite* datahub_suite(void) {
    Suite* s = suite_create("DataHub");
    
    TCase* tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_hub_init_deinit);
    tcase_add_test(tc_core, test_node_count);
    tcase_add_test(tc_core, test_node_init_deinit);
    tcase_add_test(tc_core, test_node_registration);
    suite_add_tcase(s, tc_core);
    
    TCase* tc_comms = tcase_create("Communications");
    tcase_add_test(tc_comms, test_subscription);
    tcase_add_test(tc_comms, test_publish);
    tcase_add_test(tc_comms, test_pull);
    tcase_add_test(tc_comms, test_notify);
    suite_add_tcase(s, tc_comms);
    
    TCase* tc_errors = tcase_create("ErrorHandling");
    tcase_add_test(tc_errors, test_error_handling);
    suite_add_tcase(s, tc_errors);
    
    return s;
}

int main(void) {
    int number_failed;
    Suite* s = datahub_suite();
    SRunner* sr = srunner_create(s);
    
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
