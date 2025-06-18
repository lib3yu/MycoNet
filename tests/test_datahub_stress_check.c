// #define _POSIX_C_SOURCE 199309L  // 启用 CLOCK_MONOTONIC

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <math.h>
#include <time.h>
#include <unistd.h> 
#include <string.h> 
#include "../src/DataHub.h"

#define NUM_SENSORS 50    // 传感器节点数量
#define NUM_CONTROLLERS 20 // 控制器节点数量
#define NUM_MONITORS 10   // 监控节点数量
#define TEST_DURATION 10  // 测试持续时间(秒)
#define MAX_THROUGHPUT 500 // 最大吞吐量(消息/秒)

// 工业控制数据结构
typedef struct {
    float temperature;
    float pressure;
    float flow_rate;
    uint32_t timestamp;
} SensorData;

typedef struct {
    float valve_position;
    float pump_speed;
    uint32_t timestamp;
} ControlCommand;

// 全局状态
static atomic_int total_messages = 0;
static atomic_int error_count = 0;
static atomic_bool test_running = false;

// 传感器节点
DataNode_t sensor_nodes[NUM_SENSORS];
SensorData sensor_cache[NUM_SENSORS];

// 控制器节点
DataNode_t controller_nodes[NUM_CONTROLLERS];
ControlCommand control_cache[NUM_CONTROLLERS];

// 监控节点
DataNode_t monitor_nodes[NUM_MONITORS];

// 回调函数
static int sensor_callback(struct DataNode* node_p, EventParam_t* param);
static int controller_callback(struct DataNode* node_p, EventParam_t* param);
static int monitor_callback(struct DataNode* node_p, EventParam_t* param);

// ================= 测试初始化 =================
void init_test_nodes(void) {
    char name[32];
    
    // 初始化传感器节点
    for (int i = 0; i < NUM_SENSORS; i++) {
        
        sensor_nodes[i] = (DataNode_t){
            .name = {0},
            .size = sizeof(SensorData),
            .conflags = CONF_CACHED,
            .event_msk = EVENT_PUBLISH | EVENT_NOTIFY,
            .event_cb = sensor_callback,
            .user_data = &sensor_cache[i]
        };
        snprintf(sensor_nodes[i].name, sizeof(name), "sensor_%d", i);
        
        DataHub_InitNode(&sensor_nodes[i]);
        DataHub_PushBackNode(&sensor_nodes[i]);
        
        // 初始化传感器缓存
        sensor_cache[i] = (SensorData){
            .temperature = 25.0f,
            .pressure = 101.3f,
            .flow_rate = 0.0f,
            .timestamp = 0
        };
    }
    
    // 初始化控制器节点
    for (int i = 0; i < NUM_CONTROLLERS; i++) {
        
        controller_nodes[i] = (DataNode_t){
            .name = {0},
            .size = sizeof(ControlCommand),
            .conflags = CONF_CACHED,
            .event_msk = EVENT_PUBLISH | EVENT_NOTIFY,
            .event_cb = controller_callback,
            .user_data = &control_cache[i]
        };
        snprintf(controller_nodes[i].name, sizeof(name), "controller_%d", i);
        
        DataHub_InitNode(&controller_nodes[i]);
        DataHub_PushBackNode(&controller_nodes[i]);
        
        // 初始化控制缓存
        control_cache[i] = (ControlCommand){
            .valve_position = 0.5f,
            .pump_speed = 50.0f,
            .timestamp = 0
        };
    }
    
    // 初始化监控节点
    for (int i = 0; i < NUM_MONITORS; i++) {
        
        monitor_nodes[i] = (DataNode_t){
            .name = {0},
            .size = 0,
            .conflags = CONF_NONE,
            .event_msk = EVENT_PUBLISH | EVENT_NOTIFY,
            .event_cb = monitor_callback
        };
        snprintf(monitor_nodes[i].name, sizeof(name), "monitor_%d", i);

        DataHub_InitNode(&monitor_nodes[i]);
        DataHub_PushBackNode(&monitor_nodes[i]);
    }
}

// ================= 回调函数 =================
static int sensor_callback(struct DataNode* node_p, EventParam_t* param) {
    // 传感器只响应通知事件
    if (param->event == EVENT_NOTIFY) {
        // 模拟传感器校准命令
        if (param->size == sizeof(float)) {
            float* cal_value = (float*)param->data_p;
            SensorData* cache = (SensorData*)node_p->user_data;
            cache->temperature += *cal_value;
            atomic_fetch_add(&total_messages, 1);
        }
    }
    return DH_OK;
}

static int controller_callback(struct DataNode* node_p, EventParam_t* param) {
    if (param->event == EVENT_PUBLISH) {
        // 处理传感器数据更新
        if (param->size == sizeof(SensorData)) {
            SensorData* data = (SensorData*)param->data_p;
            ControlCommand* cache = (ControlCommand*)node_p->user_data;
            
            // 简单的控制逻辑
            if (data->temperature > 30.0f) {
                cache->valve_position = fminf(cache->valve_position + 0.1f, 1.0f);
            } else if (data->temperature < 20.0f) {
                cache->valve_position = fmaxf(cache->valve_position - 0.1f, 0.0f);
            }
            
            cache->timestamp = data->timestamp;
            
            // 发布控制命令
            DataHub_NodePublish(node_p, cache, sizeof(ControlCommand));
            atomic_fetch_add(&total_messages, 1);
        }
    } else if (param->event == EVENT_NOTIFY) {
        // 处理控制参数调整
        if (param->size == sizeof(ControlCommand)) {
            ControlCommand* cmd = (ControlCommand*)param->data_p;
            ControlCommand* cache = (ControlCommand*)node_p->user_data;
            *cache = *cmd;
            atomic_fetch_add(&total_messages, 1);
        }
    }
    return DH_OK;
}

static int monitor_callback(struct DataNode* node_p, EventParam_t* param) {
    // 监控节点记录所有事件
    atomic_fetch_add(&total_messages, 1);
    return DH_OK;
}

// ================= 线程函数 =================
void* sensor_thread(void* arg) {
    int sensor_id = *(int*)arg;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while (test_running) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec) + 
                        (now.tv_nsec - start.tv_nsec) / 1e9;
        
        // 控制发布频率
        if (elapsed > 1.0 / MAX_THROUGHPUT) {
            // 更新传感器数据
            SensorData* data = &sensor_cache[sensor_id];
            data->temperature = 25.0f + 10.0f * sinf(elapsed + sensor_id);
            data->pressure = 100.0f + 10.0f * cosf(elapsed + sensor_id);
            data->flow_rate = 5.0f + 3.0f * sinf(2.0f * elapsed + sensor_id);
            data->timestamp = (uint32_t)(elapsed * 1000);
            
            // 发布数据
            int result = DataHub_NodePublish(
                &sensor_nodes[sensor_id], 
                data, 
                sizeof(SensorData)
            );
            
            if (result != DH_OK) {
                atomic_fetch_add(&error_count, 1);
            }
            
            clock_gettime(CLOCK_MONOTONIC, &start);
        }
    }
    return NULL;
}

void* controller_thread(void* arg) {
    int controller_id = *(int*)arg;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // 订阅相关传感器
    for (int i = 0; i < NUM_SENSORS; i++) {
        if (i % NUM_CONTROLLERS == controller_id) {
            DataHub_NodeSubscribe(
                &controller_nodes[controller_id],
                sensor_nodes[i].name
            );
        }
    }
    
    // 订阅其他控制器
    for (int i = 0; i < NUM_CONTROLLERS; i++) {
        if (i != controller_id) {
            DataHub_NodeSubscribe(
                &controller_nodes[controller_id],
                controller_nodes[i].name
            );
        }
    }
    
    while (test_running) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec) + 
                        (now.tv_nsec - start.tv_nsec) / 1e9;
        
        // 定期调整控制参数
        if (elapsed > 0.5) {
            ControlCommand cmd;
            cmd.valve_position = 0.5f + 0.2f * sinf(elapsed);
            cmd.pump_speed = 50.0f + 20.0f * cosf(elapsed);
            cmd.timestamp = (uint32_t)(elapsed * 1000);
            
            // 通知所有控制器更新参数
            for (int i = 0; i < NUM_CONTROLLERS; i++) {
                if (i != controller_id) {
                    DataHub_NodeNotify(
                        &controller_nodes[controller_id],
                        controller_nodes[i].name,
                        &cmd,
                        sizeof(ControlCommand)
                    );
                }
            }
            
            clock_gettime(CLOCK_MONOTONIC, &start);
        }
    }
    return NULL;
}

void* monitor_thread(void* arg) {
    int monitor_id = *(int*)arg;
    
    // 订阅所有传感器和控制器
    for (int i = 0; i < NUM_SENSORS; i++) {
        DataHub_NodeSubscribe(
            &monitor_nodes[monitor_id],
            sensor_nodes[i].name
        );
    }
    
    for (int i = 0; i < NUM_CONTROLLERS; i++) {
        DataHub_NodeSubscribe(
            &monitor_nodes[monitor_id],
            controller_nodes[i].name
        );
    }
    
    while (test_running) {
        // 监控线程主要被动接收数据
        usleep(10000); // 10ms
    }
    return NULL;
}

// ================= 压力测试用例 =================
START_TEST(test_industrial_stress) {
    // 初始化DataHub
    ck_assert_int_eq(DataHub_Init(), DH_OK);
    init_test_nodes();
    
    // 创建线程
    pthread_t sensor_threads[NUM_SENSORS];
    pthread_t controller_threads[NUM_CONTROLLERS];
    pthread_t monitor_threads[NUM_MONITORS];
    
    int sensor_ids[NUM_SENSORS];
    int controller_ids[NUM_CONTROLLERS];
    int monitor_ids[NUM_MONITORS];
    
    // 启动测试
    test_running = true;
    total_messages = 0;
    error_count = 0;
    
    // 创建传感器线程
    for (int i = 0; i < NUM_SENSORS; i++) {
        sensor_ids[i] = i;
        pthread_create(&sensor_threads[i], NULL, sensor_thread, &sensor_ids[i]);
    }
    
    // 创建控制器线程
    for (int i = 0; i < NUM_CONTROLLERS; i++) {
        controller_ids[i] = i;
        pthread_create(&controller_threads[i], NULL, controller_thread, &controller_ids[i]);
    }
    
    // 创建监控线程
    for (int i = 0; i < NUM_MONITORS; i++) {
        monitor_ids[i] = i;
        pthread_create(&monitor_threads[i], NULL, monitor_thread, &monitor_ids[i]);
    }
    
    // 运行测试
    time_t start_time = time(NULL);
    while (time(NULL) - start_time < TEST_DURATION) {
        sleep(1);
        printf("Total Messages : %d, Errors: %d\n",
                total_messages, error_count);
    }
    
    // 停止测试
    test_running = false;
    
    // 等待所有线程结束
    for (int i = 0; i < NUM_SENSORS; i++) {
        pthread_join(sensor_threads[i], NULL);
    }
    for (int i = 0; i < NUM_CONTROLLERS; i++) {
        pthread_join(controller_threads[i], NULL);
    }
    for (int i = 0; i < NUM_MONITORS; i++) {
        pthread_join(monitor_threads[i], NULL);
    }
    
    // 输出最终结果
    printf("\n=== Test Results ===\n");
    printf("Total messages: %d\n", total_messages);
    printf("Errors: %d\n", error_count);
    printf("Message rate: %.2f msg/s\n", 
           (double)total_messages / TEST_DURATION);
    
    // 验证无错误发生
    ck_assert_int_eq(error_count, 0);
    
    DataHub_Deinit();
}
END_TEST

// ================= 测试套件配置 =================
Suite* datahub_suite(void) {
    Suite* s = suite_create("DataHub Stress Test");
    
    TCase* tc_stress = tcase_create("IndustrialStress");
    tcase_set_timeout(tc_stress, TEST_DURATION + 5); // 设置超时
    tcase_add_test(tc_stress, test_industrial_stress);
    suite_add_tcase(s, tc_stress);
    
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

