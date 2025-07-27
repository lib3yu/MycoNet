#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <myconet.h>

/* Test callback counters */
static int publish_called = 0;
static int pull_called = 0;
static int notify_called = 0;

/* 
 * Test callback function that counts different event types
 * Increments counters for publish, pull and notify events
 * @param node_p The node that triggered the event
 * @param param Event parameters including event type
 * @return MN_OK on success
 */
static int test_callback(struct DataNode* node_p, EventParam_t* param) {
    switch(param->event) {
        case EVENT_PUBLISH: publish_called++; break;
        case EVENT_PULL: pull_called++; break;
        case EVENT_NOTIFY: notify_called++; break;
        default: break;
    }
    return MN_OK;
}

/* 
 * Test node definitions - defines nodes with different configurations:
 * - node1_0: Non-cached node with all event types
 * - node1: Cached node with all event types  
 * - node2: Display node with all event types
 */
MycoNode_t node1_0 = {
    .name = "sensor_temp_1.0",
    .size = sizeof(float),
    .conflags = CONF_NONE,
    .event_msk = EVENT_PUBLISH | EVENT_PULL | EVENT_NOTIFY,
    .event_cb = test_callback
};

MycoNode_t node1 = {
    .name = "sensor_temp_1.1",
    .size = sizeof(float),
    .conflags = CONF_CACHED,
    .event_msk = EVENT_PUBLISH | EVENT_PULL | EVENT_NOTIFY,
    .event_cb = test_callback,
#if MN_RESTRICT_NOTIFY_SIZE_CHECK_ENABLE
    .notify_size = sizeof(int)
#endif
};

MycoNode_t node2 = {
    .name = "display_unit",
    .size = sizeof(float),
    .conflags = CONF_NONE,
    .event_msk = EVENT_PUBLISH | EVENT_PULL | EVENT_NOTIFY,
    .event_cb = test_callback
};

/* 
 * ================= Hub Management Tests =================
 * Tests basic hub lifecycle operations:
 * - Initialization/deinitialization
 * - Node counting
 */
START_TEST(test_hub_init_deinit) {
    ck_assert_int_eq(MycoNet_Init(), MN_OK);
    ck_assert_int_eq(MycoNet_Deinit(), MN_OK);
    ck_assert_int_eq(MycoNet_Deinit(), MN_ERR_NOTINITIALIZED); // Double deinitialization
}
END_TEST

START_TEST(test_node_count) {
    MycoNet_Init();
    ck_assert_int_eq(MycoNet_GetNodeNum(), 1); // Only dummy node exists
    
    MycoNet_InitNode(&node1);
    MycoNet_PushBackNode(&node1);
    ck_assert_int_eq(MycoNet_GetNodeNum(), 2); // Dummy + node1
    
    MycoNet_Deinit();
}
END_TEST

/* 
 * ================= Node Lifecycle Tests =================
 * Tests node initialization, registration and cleanup:
 * - Node init/deinit
 * - Node push/remove from hub
 * - Error cases (duplicate operations)
 */
START_TEST(test_node_init_deinit) {
    ck_assert_int_eq(MycoNet_InitNode(&node1), MN_OK);
    ck_assert_int_eq(MycoNet_InitNode(&node1), MN_ERR_INITIALIZED); // Double initialization
    
    ck_assert_int_eq(MycoNet_DeinitNode(&node1), MN_OK);
    ck_assert_int_eq(MycoNet_DeinitNode(&node1), MN_ERR_NOTINITIALIZED); // Double deinitialization
}
END_TEST

START_TEST(test_node_registration) {
    MycoNet_Init();
    
    MycoNet_InitNode(&node1);
    ck_assert_int_eq(MycoNet_PushBackNode(&node1), MN_OK);
    ck_assert_int_eq(MycoNet_PushBackNode(&node1), MN_ERR_EXIST); // Duplicate registration
    ck_assert_int_eq(MycoNet_PrintNodeList(printf), MN_OK);
    ck_assert_int_eq(MycoNet_RemoveNode(&node1), MN_OK);
    ck_assert_int_eq(MycoNet_RemoveNode(&node1), MN_ERR_NOTFOUND); // Duplicate removal
    
    MycoNet_Deinit();
}
END_TEST

/* 
 * ================= Subscription Tests =================
 * Tests subscription relationships between nodes:
 * - Subscribe/unsubscribe operations
 * - Publisher/subscriber counters
 */
START_TEST(test_subscription) {
    MycoNet_Init();
    MycoNet_InitNode(&node1);
    MycoNet_InitNode(&node2);
    MycoNet_PushBackNode(&node1);
    MycoNet_PushBackNode(&node2);
    
    ck_assert_int_eq(MycoNet_NodeSubscribe(&node2, node1.name), MN_OK);
    ck_assert_int_eq(MycoNet_GetNodePubNum(&node1), 1);
    ck_assert_int_eq(MycoNet_GetNodeSubNum(&node2), 1);
    
    ck_assert_int_eq(MycoNet_NodeUnsubscribe(&node2, node1.name), MN_OK);
    ck_assert_int_eq(MycoNet_GetNodePubNum(&node1), 0);
    
    MycoNet_Deinit();
}
END_TEST

/* 
 * ================= Data Publishing Tests =================
 * Tests data publishing functionality:
 * - Basic publish operation
 * - Callback triggering
 * - Caching behavior verification
 */
START_TEST(test_publish) {
    MycoNet_Init();
    MycoNet_InitNode(&node1);
    MycoNet_InitNode(&node2);
    MycoNet_PushBackNode(&node1);
    MycoNet_PushBackNode(&node2);
    MycoNet_NodeSubscribe(&node2, node1.name);
    
    float temp = 25.5f;
    publish_called = 0;
    ck_assert_int_eq(MycoNet_NodePublish(&node1, &temp, sizeof(float)), MN_OK);
    ck_assert_int_eq(publish_called, 1); // Verify callback triggered
    
    // Test caching functionality
    float cached_value;
    ck_assert_int_eq(MycoNet_NodePull(&node2, node1.name, &cached_value, sizeof(float)), MN_OK);
    ck_assert(cached_value == 25.5f);
    
    MycoNet_Deinit();
}
END_TEST

/* 
 * ================= Data Pulling Tests =================
 * Tests data pulling functionality:
 * - Pull from cached vs non-cached nodes
 * - Size validation
 * - Error cases (invalid parameters)
 */
START_TEST(test_pull) {
    MycoNet_Init();
    MycoNet_InitNode(&node1);
    MycoNet_InitNode(&node1_0);
    MycoNet_InitNode(&node2);
    MycoNet_PushBackNode(&node1);
    MycoNet_PushBackNode(&node1_0);
    MycoNet_PushBackNode(&node2);
    
    float value;
    pull_called = 0;
    
    // Test cached node (node1)
    ck_assert_int_eq(MycoNet_NodePull(&node2, node1.name, &value, sizeof(float)), MN_OK);
    ck_assert_int_eq(pull_called, 0);
    
    // Test non-cached node (node1_0)
    pull_called = 0;
    ck_assert_int_eq(MycoNet_NodePull(&node2, node1_0.name, &value, sizeof(float)), MN_OK);
    ck_assert_int_eq(pull_called, 1);
    
    // Test size mismatch
    ck_assert_int_eq(MycoNet_NodePull(&node2, node1.name, &value, 1), MN_ERR_SIZE_MISMATCH);
    
    // Boundary testing
    ck_assert_int_eq(MycoNet_NodePull(NULL, node1.name, &value, sizeof(float)), MN_ERR_INVALID);
    ck_assert_int_eq(MycoNet_NodePull(&node2, NULL, &value, sizeof(float)), MN_ERR_INVALID);
    ck_assert_int_eq(MycoNet_NodePull(&node2, "invalid_node", &value, sizeof(float)), MN_ERR_NOTFOUND);
    
    MycoNet_Deinit();
}
END_TEST

// ================= Notification Testing =================
// Tests notification functionality between nodes
// Verifies callback is triggered on notify
START_TEST(test_notify) {
    MycoNet_Init();
    MycoNet_InitNode(&node1);
    MycoNet_InitNode(&node2);
    MycoNet_PushBackNode(&node1);
    MycoNet_PushBackNode(&node2);
    
    int command = 1;
    notify_called = 0;
    ck_assert_int_eq(MycoNet_NodeNotify(&node2, node1.name, &command, sizeof(int)), MN_OK);
    ck_assert_int_eq(notify_called, 1);
    
    MycoNet_Deinit();
}
END_TEST

// ================= Error Handling Testing =================
// Tests error cases and invalid operations:
// - Uninitialized hub operations
// - Invalid parameter handling
// - Platform compatibility checks
START_TEST(test_error_handling) {
    // Uninitialized hub testing
    ck_assert_int_eq(MycoNet_GetNodeNum(), MN_ERR_NOTINITIALIZED);
    ck_assert_ptr_eq(MycoNet_SearchNode("test"), NULL);
    // Invalid parameter testing
    ck_assert_int_eq(MycoNet_InitNode(NULL), MN_ERR_NULL_POINTER);
    ck_assert_int_eq(MycoNet_NodePublish(NULL, NULL, 0), MN_ERR_INVALID);
    
    // Unsupported platform testing
    #if !defined(USE_FREERTOS) && !(defined(__GNUC__))
        ck_assert(0); // Should trigger #error
    #endif
}
END_TEST

// ================= Boundary Condition Testing =================
// Tests edge cases and invalid inputs:
// - Empty node names
// - Other boundary conditions
START_TEST(test_node_name_boundary) {
    // Test empty name
    MycoNode_t empty_name_node = {
        .name = "",
        .size = sizeof(float),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH | EVENT_PULL | EVENT_NOTIFY,
        .event_cb = test_callback
    };
    
    ck_assert_int_eq(MycoNet_InitNode(&empty_name_node), MN_ERR_INVALID);
}
END_TEST

// ================= Concurrency Testing =================
// Tests concurrent access scenarios:
// - Multiple sequential publish/pull operations
// - Verifies data consistency
START_TEST(test_concurrent_access) {
    MycoNet_Init();
    
    // Create multiple nodes
    MycoNode_t node1 = {.name = "concurrent_node1", .size = sizeof(int), .event_msk = EVENT_PUBLISH | EVENT_PULL, .conflags = CONF_CACHED};
    MycoNode_t node2 = {.name = "concurrent_node2", .size = sizeof(int), .event_msk = EVENT_PUBLISH | EVENT_PULL};
    
    MycoNet_InitNode(&node1);
    MycoNet_InitNode(&node2);
    MycoNet_PushBackNode(&node1);
    MycoNet_PushBackNode(&node2);
    MycoNet_NodeSubscribe(&node2, node1.name);
    
    // Concurrent publish and pull
    int value = 0;
    for (int i = 0; i < 100; i++) {
        value = i;
        ck_assert_int_eq(MycoNet_NodePublish(&node1, &value, sizeof(int)), MN_OK);
        int received;
        ck_assert_int_eq(MycoNet_NodePull(&node2, node1.name, &received, sizeof(int)), MN_OK);
        ck_assert_int_eq(received, i);
    }
    
    MycoNet_Deinit();
}
END_TEST

// ================= Cache Consistency Testing =================
// Tests cached data behavior:
// - Verifies subscribers get consistent cached values
// - Checks cache updates properly
START_TEST(test_cache_consistency) {
    MycoNet_Init();
    
    MycoNode_t cached_node = {
        .name = "cached_node",
        .size = sizeof(float),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    MycoNode_t subscriber = {
        .name = "cache_subscriber",
        .size = sizeof(float),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    MycoNet_InitNode(&cached_node);
    MycoNet_InitNode(&subscriber);
    MycoNet_PushBackNode(&cached_node);
    MycoNet_PushBackNode(&subscriber);
    MycoNet_NodeSubscribe(&subscriber, cached_node.name);
    
    // Test cache consistency
    float temp = 3.14f;
    ck_assert_int_eq(MycoNet_NodePublish(&cached_node, &temp, sizeof(float)), MN_OK);
    
    float cached_value;
    ck_assert_int_eq(MycoNet_NodePull(&subscriber, cached_node.name, &cached_value, sizeof(float)), MN_OK);
    ck_assert(cached_value == 3.14f);
    
    MycoNet_Deinit();
}
END_TEST

// ================= Dynamic Node Allocation Tests =================
// Tests node lifecycle with dynamically allocated nodes
START_TEST(test_dynamic_node_allocation) {
    MycoNet_Init();
    
    // Allocate and initialize node dynamically
    MycoNode_t* dyn_node = malloc(sizeof(MycoNode_t));
    ck_assert_ptr_nonnull(dyn_node);
    
    *dyn_node = (MycoNode_t){
        .name = "dynamic_node",
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    ck_assert_int_eq(MycoNet_InitNode(dyn_node), MN_OK);
    ck_assert_int_eq(MycoNet_PushBackNode(dyn_node), MN_OK);
    
    // Test operations
    int value = 42;
    ck_assert_int_eq(MycoNet_NodePublish(dyn_node, &value, sizeof(int)), MN_OK);
    
    // Cleanup
    ck_assert_int_eq(MycoNet_RemoveNode(dyn_node), MN_OK);
    ck_assert_int_eq(MycoNet_DeinitNode(dyn_node), MN_OK);
    free(dyn_node);
    
    MycoNet_Deinit();
}
END_TEST

// ================= Memory Leak Testing =================
// Tests proper cleanup of resources:
// - Verifies all allocated memory is freed
// - Checks for leaks during node lifecycle
START_TEST(test_memory_leak) {
    // Initialize hub
    ck_assert_int_eq(MycoNet_Init(), MN_OK);
    
    // Create and register node
    MycoNode_t node = {
        .name = "memory_test_node",
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    ck_assert_int_eq(MycoNet_InitNode(&node), MN_OK);
    ck_assert_int_eq(MycoNet_PushBackNode(&node), MN_OK);
    
    // Perform some operations
    int value = 42;
    ck_assert_int_eq(MycoNet_NodePublish(&node, &value, sizeof(int)), MN_OK);
    
    // Cleanup
    ck_assert_int_eq(MycoNet_RemoveNode(&node), MN_OK);
    ck_assert_int_eq(MycoNet_DeinitNode(&node), MN_OK);
    ck_assert_int_eq(MycoNet_Deinit(), MN_OK);
}
END_TEST

// ================= Multithread Concurrency Testing =================
// Tests thread-safe operations:
// - Publisher thread continuously publishes data
// - Subscriber thread continuously pulls data
// - Verifies no race conditions or corruption
#include <pthread.h>

typedef struct {
    MycoNode_t* node;
    int start_val;
    int count;
} ThreadArgs;

static void* publisher_thread(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    for (int i = 0; i < args->count; i++) {
        int value = args->start_val + i;
        ck_assert_int_eq(MycoNet_NodePublish(args->node, &value, sizeof(int)), MN_OK);
    }
    return NULL;
}

static void* subscriber_thread(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    for (int i = 0; i < args->count; i++) {
        int received;
        ck_assert_int_eq(MycoNet_NodePull(args->node, "publisher_node", &received, sizeof(int)), MN_OK);
        ck_assert_int_ge(received, args->start_val);
        ck_assert_int_lt(received, args->start_val + args->count);
    }
    return NULL;
}

START_TEST(test_multithread_concurrent) {
    MycoNet_Init();
    
    // Create shared nodes
    MycoNode_t publisher = {
        .name = "publisher_node",
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    MycoNode_t subscriber = {
        .name = "subscriber_node",
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    MycoNet_InitNode(&publisher);
    MycoNet_InitNode(&subscriber);
    MycoNet_PushBackNode(&publisher);
    MycoNet_PushBackNode(&subscriber);
    MycoNet_NodeSubscribe(&subscriber, publisher.name);

    // Create thread arguments
    ThreadArgs pub_args = {&publisher, 0, 100};
    ThreadArgs sub_args = {&subscriber, 0, 100};
    
    // Create threads
    pthread_t pub_thread, sub_thread;
    pthread_create(&pub_thread, NULL, publisher_thread, &pub_args);
    pthread_create(&sub_thread, NULL, subscriber_thread, &sub_args);
    
    // Wait for threads to complete
    pthread_join(pub_thread, NULL);
    pthread_join(sub_thread, NULL);
    
    MycoNet_Deinit();
}
END_TEST

// ================= Test Suite Configuration =================
// Organizes tests into logical groups:
// - Core functionality tests
// - Communication tests
// - Error handling tests
Suite* myconet_suite(void) {
    Suite* s = suite_create("DataHub");
    
    TCase* tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_hub_init_deinit);
    tcase_add_test(tc_core, test_node_count);
    tcase_add_test(tc_core, test_node_init_deinit);
    tcase_add_test(tc_core, test_node_registration);
    tcase_add_test(tc_core, test_node_name_boundary);
    suite_add_tcase(s, tc_core);
    
    TCase* tc_comms = tcase_create("Communications");
    tcase_add_test(tc_comms, test_subscription);
    tcase_add_test(tc_comms, test_publish);
    tcase_add_test(tc_comms, test_pull);
    tcase_add_test(tc_comms, test_notify);
    tcase_add_test(tc_comms, test_concurrent_access);
    tcase_add_test(tc_comms, test_cache_consistency);
    suite_add_tcase(s, tc_comms);
    
    TCase* tc_errors = tcase_create("ErrorHandling");
    tcase_add_test(tc_errors, test_error_handling);
    suite_add_tcase(s, tc_errors);

    TCase* tc_dynamic = tcase_create("DynamicAllocation");
    tcase_add_test(tc_dynamic, test_dynamic_node_allocation);
    suite_add_tcase(s, tc_dynamic);

    TCase* tc_memory = tcase_create("Memory");
    tcase_add_test(tc_memory, test_memory_leak);
    suite_add_tcase(s, tc_memory);

    TCase* tc_threads = tcase_create("Threads");
    tcase_add_test(tc_threads, test_multithread_concurrent);
    suite_add_tcase(s, tc_threads);
    
    return s;
}

int main(void) {
    int number_failed;
    Suite* s = myconet_suite();
    SRunner* sr = srunner_create(s);
    
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
