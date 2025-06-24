#include <check.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../src/DataHub.h"

/* Test callback counters */
static int publish_called = 0;
static int pull_called = 0;
static int notify_called = 0;

/* 
 * Test callback function that counts different event types
 * Increments counters for publish, pull and notify events
 * @param node_p The node that triggered the event
 * @param param Event parameters including event type
 * @return DH_OK on success
 */
static int test_callback(struct DataNode* node_p, EventParam_t* param) {
    switch(param->event) {
        case EVENT_PUBLISH: publish_called++; break;
        case EVENT_PULL: pull_called++; break;
        case EVENT_NOTIFY: notify_called++; break;
        default: break;
    }
    return DH_OK;
}

/* 
 * Test node definitions - defines nodes with different configurations:
 * - node1_0: Non-cached node with all event types
 * - node1: Cached node with all event types  
 * - node2: Display node with all event types
 */
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

/* 
 * ================= Hub Management Tests =================
 * Tests basic hub lifecycle operations:
 * - Initialization/deinitialization
 * - Node counting
 */
START_TEST(test_hub_init_deinit) {
    ck_assert_int_eq(DataHub_Init(), DH_OK);
    ck_assert_int_eq(DataHub_Deinit(), DH_OK);
    ck_assert_int_eq(DataHub_Deinit(), DH_ERR_NOTINITIALIZED); // Double deinitialization
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

/* 
 * ================= Node Lifecycle Tests =================
 * Tests node initialization, registration and cleanup:
 * - Node init/deinit
 * - Node push/remove from hub
 * - Error cases (duplicate operations)
 */
START_TEST(test_node_init_deinit) {
    ck_assert_int_eq(DataHub_InitNode(&node1), DH_OK);
    ck_assert_int_eq(DataHub_InitNode(&node1), DH_ERR_INITIALIZED); // Double initialization
    
    ck_assert_int_eq(DataHub_DeinitNode(&node1), DH_OK);
    ck_assert_int_eq(DataHub_DeinitNode(&node1), DH_ERR_NOTINITIALIZED); // Double deinitialization
}
END_TEST

START_TEST(test_node_registration) {
    DataHub_Init();
    
    DataHub_InitNode(&node1);
    ck_assert_int_eq(DataHub_PushBackNode(&node1), DH_OK);
    ck_assert_int_eq(DataHub_PushBackNode(&node1), DH_ERR_EXIST); // Duplicate registration
    
    ck_assert_int_eq(DataHub_RemoveNode(&node1), DH_OK);
    ck_assert_int_eq(DataHub_RemoveNode(&node1), DH_ERR_NOTFOUND); // Duplicate removal
    
    DataHub_Deinit();
}
END_TEST

/* 
 * ================= Subscription Tests =================
 * Tests subscription relationships between nodes:
 * - Subscribe/unsubscribe operations
 * - Publisher/subscriber counters
 */
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

/* 
 * ================= Data Publishing Tests =================
 * Tests data publishing functionality:
 * - Basic publish operation
 * - Callback triggering
 * - Caching behavior verification
 */
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
    ck_assert_int_eq(publish_called, 1); // Verify callback triggered
    
    // Test caching functionality
    float cached_value;
    ck_assert_int_eq(DataHub_NodePull(&node2, node1.name, &cached_value, sizeof(float)), DH_OK);
    ck_assert(cached_value == 25.5f);
    
    DataHub_Deinit();
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
    DataHub_Init();
    DataHub_InitNode(&node1);
    DataHub_InitNode(&node1_0);
    DataHub_InitNode(&node2);
    DataHub_PushBackNode(&node1);
    DataHub_PushBackNode(&node1_0);
    DataHub_PushBackNode(&node2);
    
    float value;
    pull_called = 0;
    
    // Test cached node (node1)
    ck_assert_int_eq(DataHub_NodePull(&node2, node1.name, &value, sizeof(float)), DH_OK);
    ck_assert_int_eq(pull_called, 0);
    
    // Test non-cached node (node1_0)
    pull_called = 0;
    ck_assert_int_eq(DataHub_NodePull(&node2, node1_0.name, &value, sizeof(float)), DH_OK);
    ck_assert_int_eq(pull_called, 1);
    
    // Test size mismatch
    ck_assert_int_eq(DataHub_NodePull(&node2, node1.name, &value, 1), DH_ERR_SIZE_MISMATCH);
    
    // Boundary testing
    ck_assert_int_eq(DataHub_NodePull(NULL, node1.name, &value, sizeof(float)), DH_ERR_INVALID);
    ck_assert_int_eq(DataHub_NodePull(&node2, NULL, &value, sizeof(float)), DH_ERR_INVALID);
    ck_assert_int_eq(DataHub_NodePull(&node2, "invalid_node", &value, sizeof(float)), DH_ERR_NOTFOUND);
    
    DataHub_Deinit();
}
END_TEST

// ================= Notification Testing =================
// Tests notification functionality between nodes
// Verifies callback is triggered on notify
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

// ================= Error Handling Testing =================
// Tests error cases and invalid operations:
// - Uninitialized hub operations
// - Invalid parameter handling
// - Platform compatibility checks
START_TEST(test_error_handling) {
    // Uninitialized hub testing
    ck_assert_int_eq(DataHub_GetNodeNum(), DH_ERR_NOTINITIALIZED);
    ck_assert_ptr_eq(DataHub_SearchNode("test"), NULL);
    // Invalid parameter testing
    ck_assert_int_eq(DataHub_InitNode(NULL), DH_ERR_INVALID);
    ck_assert_int_eq(DataHub_NodePublish(NULL, NULL, 0), DH_ERR_INVALID);
    
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
    DataNode_t empty_name_node = {
        .name = "",
        .size = sizeof(float),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH | EVENT_PULL | EVENT_NOTIFY,
        .event_cb = test_callback
    };
    
    ck_assert_int_eq(DataHub_InitNode(&empty_name_node), DH_ERR_INVALID);
}
END_TEST

// ================= Concurrency Testing =================
// Tests concurrent access scenarios:
// - Multiple sequential publish/pull operations
// - Verifies data consistency
START_TEST(test_concurrent_access) {
    DataHub_Init();
    
    // Create multiple nodes
    DataNode_t node1 = {.name = "concurrent_node1", .size = sizeof(int), .event_msk = EVENT_PUBLISH | EVENT_PULL, .conflags = CONF_CACHED};
    DataNode_t node2 = {.name = "concurrent_node2", .size = sizeof(int), .event_msk = EVENT_PUBLISH | EVENT_PULL};
    
    DataHub_InitNode(&node1);
    DataHub_InitNode(&node2);
    DataHub_PushBackNode(&node1);
    DataHub_PushBackNode(&node2);
    DataHub_NodeSubscribe(&node2, node1.name);
    
    // Concurrent publish and pull
    int value = 0;
    for (int i = 0; i < 100; i++) {
        value = i;
        ck_assert_int_eq(DataHub_NodePublish(&node1, &value, sizeof(int)), DH_OK);
        int received;
        ck_assert_int_eq(DataHub_NodePull(&node2, node1.name, &received, sizeof(int)), DH_OK);
        ck_assert_int_eq(received, i);
    }
    
    DataHub_Deinit();
}
END_TEST

// ================= Cache Consistency Testing =================
// Tests cached data behavior:
// - Verifies subscribers get consistent cached values
// - Checks cache updates properly
START_TEST(test_cache_consistency) {
    DataHub_Init();
    
    DataNode_t cached_node = {
        .name = "cached_node",
        .size = sizeof(float),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    DataNode_t subscriber = {
        .name = "cache_subscriber",
        .size = sizeof(float),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    DataHub_InitNode(&cached_node);
    DataHub_InitNode(&subscriber);
    DataHub_PushBackNode(&cached_node);
    DataHub_PushBackNode(&subscriber);
    DataHub_NodeSubscribe(&subscriber, cached_node.name);
    
    // Test cache consistency
    float temp = 3.14f;
    ck_assert_int_eq(DataHub_NodePublish(&cached_node, &temp, sizeof(float)), DH_OK);
    
    float cached_value;
    ck_assert_int_eq(DataHub_NodePull(&subscriber, cached_node.name, &cached_value, sizeof(float)), DH_OK);
    ck_assert(cached_value == 3.14f);
    
    DataHub_Deinit();
}
END_TEST

// ================= Dynamic Node Allocation Tests =================
// Tests node lifecycle with dynamically allocated nodes
START_TEST(test_dynamic_node_allocation) {
    DataHub_Init();
    
    // Allocate and initialize node dynamically
    DataNode_t* dyn_node = malloc(sizeof(DataNode_t));
    ck_assert_ptr_nonnull(dyn_node);
    
    *dyn_node = (DataNode_t){
        .name = "dynamic_node",
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    ck_assert_int_eq(DataHub_InitNode(dyn_node), DH_OK);
    ck_assert_int_eq(DataHub_PushBackNode(dyn_node), DH_OK);
    
    // Test operations
    int value = 42;
    ck_assert_int_eq(DataHub_NodePublish(dyn_node, &value, sizeof(int)), DH_OK);
    
    // Cleanup
    ck_assert_int_eq(DataHub_RemoveNode(dyn_node), DH_OK);
    ck_assert_int_eq(DataHub_DeinitNode(dyn_node), DH_OK);
    free(dyn_node);
    
    DataHub_Deinit();
}
END_TEST

// ================= Memory Leak Testing =================
// Tests proper cleanup of resources:
// - Verifies all allocated memory is freed
// - Checks for leaks during node lifecycle
START_TEST(test_memory_leak) {
    // Initialize hub
    ck_assert_int_eq(DataHub_Init(), DH_OK);
    
    // Create and register node
    DataNode_t node = {
        .name = "memory_test_node",
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    ck_assert_int_eq(DataHub_InitNode(&node), DH_OK);
    ck_assert_int_eq(DataHub_PushBackNode(&node), DH_OK);
    
    // Perform some operations
    int value = 42;
    ck_assert_int_eq(DataHub_NodePublish(&node, &value, sizeof(int)), DH_OK);
    
    // Cleanup
    ck_assert_int_eq(DataHub_RemoveNode(&node), DH_OK);
    ck_assert_int_eq(DataHub_DeinitNode(&node), DH_OK);
    ck_assert_int_eq(DataHub_Deinit(), DH_OK);
}
END_TEST

// ================= Multithread Concurrency Testing =================
// Tests thread-safe operations:
// - Publisher thread continuously publishes data
// - Subscriber thread continuously pulls data
// - Verifies no race conditions or corruption
#include <pthread.h>

typedef struct {
    DataNode_t* node;
    int start_val;
    int count;
} ThreadArgs;

static void* publisher_thread(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    for (int i = 0; i < args->count; i++) {
        int value = args->start_val + i;
        ck_assert_int_eq(DataHub_NodePublish(args->node, &value, sizeof(int)), DH_OK);
    }
    return NULL;
}

static void* subscriber_thread(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    for (int i = 0; i < args->count; i++) {
        int received;
        ck_assert_int_eq(DataHub_NodePull(args->node, "publisher_node", &received, sizeof(int)), DH_OK);
        ck_assert_int_ge(received, args->start_val);
        ck_assert_int_lt(received, args->start_val + args->count);
    }
    return NULL;
}

START_TEST(test_multithread_concurrent) {
    DataHub_Init();
    
    // Create shared nodes
    DataNode_t publisher = {
        .name = "publisher_node",
        .size = sizeof(int),
        .conflags = CONF_CACHED,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    DataNode_t subscriber = {
        .name = "subscriber_node",
        .size = sizeof(int),
        .conflags = CONF_NONE,
        .event_msk = EVENT_PUBLISH | EVENT_PULL,
        .event_cb = test_callback
    };
    
    DataHub_InitNode(&publisher);
    DataHub_InitNode(&subscriber);
    DataHub_PushBackNode(&publisher);
    DataHub_PushBackNode(&subscriber);
    DataHub_NodeSubscribe(&subscriber, publisher.name);

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
    
    DataHub_Deinit();
}
END_TEST

// ================= Test Suite Configuration =================
// Organizes tests into logical groups:
// - Core functionality tests
// - Communication tests
// - Error handling tests
Suite* datahub_suite(void) {
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
    Suite* s = datahub_suite();
    SRunner* sr = srunner_create(s);
    
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
