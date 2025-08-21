#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>    // for sleep()
#include <stdatomic.h> // for atomic_bool (C11 standard)
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include "myconet.h" // 引入C-API头文件

// ====================================================================
// 1. 全局控制变量和线程安全打印
// ====================================================================

static atomic_bool g_system_running = true;
static pthread_mutex_t g_print_mutex;

// 线程安全的打印函数
void print_safe(const char* source, const char* message) 
{
    pthread_mutex_lock(&g_print_mutex);
    printf("[%-20s] %s\n", source, message);
    pthread_mutex_unlock(&g_print_mutex);
}

// ====================================================================
// 2. 消息数据结构定义 (纯C)
// ====================================================================
typedef struct {
    double temperature;
    long timestamp;
} TemperatureData;

typedef struct {
    int turn_on; // C语言中没有bool，通常用int
    int fan_speed;
} FanCommand;

// ====================================================================
// 3. 节点回调函数 (纯C函数指针)
// ====================================================================

// 控制器的事件回调
void controller_event_cb(const MycoNet_EventParam_t *param) {
    char buffer[256];
    // 处理来自传感器的温度发布
    if (param->event == EVENT_PUBLISH && param->size == sizeof(TemperatureData)) {
        const TemperatureData* data = (const TemperatureData*)param->data_p;
        sprintf(buffer, "Received temperature: %.2f C", data->temperature);
        print_safe("CoolingController CB", buffer);

        // 控制逻辑：温度高于28度则开启风扇
        if (data->temperature > 28.0) {
            FanCommand cmd = {1, 80}; // turn_on = true, speed = 80%
            print_safe("CoolingController CB", "Temp HIGH! Publishing FAN ON command.");
            // 使用 param->recver 获取自己的ID来发布消息
            myconet_publish(param->recver, &cmd, sizeof(cmd));
        }
    }
    // 处理来自HMI的紧急通知
    else if (param->event == EVENT_NOTIFY && param->size == sizeof(FanCommand)) {
        const FanCommand* cmd = (const FanCommand*)param->data_p;
        if (cmd->turn_on) {
            print_safe("CoolingController CB", "Received URGENT NOTIFY from HMI. Forcing FAN ON.");
            // 使用 param->recver 获取自己的ID来发布消息
            myconet_publish(param->recver, cmd, sizeof(*cmd));
        }
    }
}

// 风扇执行器的事件回调
void fan_actuator_event_cb(const MycoNet_EventParam_t *param) {
    if (param->event == EVENT_PUBLISH && param->size == sizeof(FanCommand)) {
        const FanCommand* cmd = (const FanCommand*)param->data_p;
        char buffer[100];
        if (cmd->turn_on) {
            sprintf(buffer, "Command received. Turning ON fan at speed %d%%", cmd->fan_speed);
        } else {
            sprintf(buffer, "Command received. Turning OFF fan.");
        }
        print_safe("FanActuator CB", buffer);
    }
}

// HMI的事件回调
void hmi_event_cb(const MycoNet_EventParam_t *param) {
    char buffer[256];
    if (param->event == EVENT_PUBLISH || param->event == EVENT_LATCHED) {
        // 在C-API中，我们无法轻易通过ID获取节点名，所以我们根据数据大小来判断
        if (param->size == sizeof(TemperatureData)) {
            const TemperatureData* data = (const TemperatureData*)param->data_p;
            const char* event_type = (param->event == EVENT_LATCHED) ? "LATCHED" : "PUBLISH";
            sprintf(buffer, "MONITOR: Temp is %.2f C. (Event: %s, from ID: %u)", data->temperature, event_type, param->sender);
            print_safe("HMI CB", buffer);
        }
    }
}

// ====================================================================
// 4. 线程函数
// ====================================================================

// 线程需要传递自己的节点ID
typedef struct {
    MycoNet_ID_t node_id;
} ThreadData;

void* sensor_thread_func(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    MycoNet_ID_t sensor_id = data->node_id;
    free(data); // 释放传递过来的数据

    char buffer[100];
    print_safe("TempSensor Thread", "Thread started.");

    while (g_system_running) {
        TemperatureData temp_data;
        temp_data.temperature = 20.0 + (rand() % 150) / 10.0; // 20.0 - 34.9 C
        temp_data.timestamp = time(NULL);

        sprintf(buffer, "Publishing temperature: %.2f C", temp_data.temperature);
        print_safe("TempSensor Thread", buffer);
        myconet_publish(sensor_id, &temp_data, sizeof(temp_data));
        
        sleep(2);
    }
    print_safe("TempSensor Thread", "Thread shutting down.");
    return NULL;
}

void* hmi_thread_func(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    MycoNet_ID_t hmi_id = data->node_id;
    free(data);

    char buffer[256];
    print_safe("HMI Thread", "Thread started.");

    // 1. 启动后立即PULL一次传感器的初始值
    sleep(1); // 等待传感器可能发布第一个值
    TemperatureData initial_temp;
    print_safe("HMI Thread", "Attempting to PULL initial temperature...");
    int ret = myconet_pull(hmi_id, "sensor/temp", &initial_temp, sizeof(initial_temp));
    if (ret == MN_OK) {
        sprintf(buffer, "PULL successful! Initial temperature: %.2f C", initial_temp.temperature);
        print_safe("HMI Thread", buffer);
    } else {
        sprintf(buffer, "PULL failed. Error: %s. This is normal if sensor hasn't published yet.", myconet_strerr(ret));
        print_safe("HMI Thread", buffer);
    }

    // 2. 模拟HMI在5秒后发送一个紧急通知
    sleep(5);
    print_safe("HMI Thread", "Sending urgent NOTIFY to controller.");
    FanCommand urgent_cmd = {1, 100}; // turn_on = true, speed = 100%
    myconet_notify(hmi_id, "controller/cooling", &urgent_cmd, sizeof(urgent_cmd));

    // 3. HMI线程继续运行，等待关闭信号
    while(g_system_running) {
        sleep(1);
    }
    print_safe("HMI Thread", "Thread shutting down.");
    return NULL;
}

// ====================================================================
// 5. 主函数 - 系统编排
// ====================================================================

int main() {
    pthread_t sensor_tid, hmi_tid;
    MycoNet_ID_t sensor_id, controller_id, fan_id, hmi_id;
    int ret;

    srand(time(NULL));
    pthread_mutex_init(&g_print_mutex, NULL);

    // --- 步骤 1: 初始化MycoNet库 ---
    print_safe("Main", "--- System Initializing ---");
    myconet_init();

    // --- 步骤 2: 创建节点 ---
    // 创建温度传感器节点 (带缓存和latching)
    MycoNet_NodeParam_t sensor_param = {
        .size = sizeof(TemperatureData),
        .conflags = CONF_CACHED, // 启用缓存
        .event_msk = EVENT_NONE, // 传感器本身不接收事件
        .event_cb = NULL
    };
    ret = myconet_create_node(&sensor_id, "sensor/temp", &sensor_param);
    if (ret != MN_OK) { print_safe("Main", "Failed to create sensor node!"); return -1; }

    // 创建控制器节点
    MycoNet_NodeParam_t controller_param = {
        .event_msk = EVENT_PUBLISH | EVENT_NOTIFY,
        .event_cb = controller_event_cb
    };
    ret = myconet_create_node(&controller_id, "controller/cooling", &controller_param);
    if (ret != MN_OK) { print_safe("Main", "Failed to create controller node!"); return -1; }

    // 创建风扇执行器节点
    MycoNet_NodeParam_t fan_param = {
        .event_msk = EVENT_PUBLISH,
        .event_cb = fan_actuator_event_cb
    };
    ret = myconet_create_node(&fan_id, "actuator/fan", &fan_param);
    if (ret != MN_OK) { print_safe("Main", "Failed to create fan node!"); return -1; }

    // 创建HMI节点
    MycoNet_NodeParam_t hmi_param = {
        .event_msk = EVENT_PUBLISH | EVENT_LATCHED,
        .event_cb = hmi_event_cb
    };
    ret = myconet_create_node(&hmi_id, "hmi/monitor", &hmi_param);
    if (ret != MN_OK) { print_safe("Main", "Failed to create HMI node!"); return -1; }

    // --- 步骤 3: 建立订阅关系 ---
    print_safe("Main", "--- Establishing Subscriptions ---");
    myconet_subscribe(controller_id, "sensor/temp");
    myconet_subscribe(fan_id, "controller/cooling");
    myconet_subscribe(hmi_id, "sensor/temp");

    // --- 步骤 4: 启动线程 ---
    print_safe("Main", "--- Starting Node Threads ---");
    ThreadData* sensor_thread_data = malloc(sizeof(ThreadData));
    sensor_thread_data->node_id = sensor_id;
    pthread_create(&sensor_tid, NULL, sensor_thread_func, sensor_thread_data);

    ThreadData* hmi_thread_data = malloc(sizeof(ThreadData));
    hmi_thread_data->node_id = hmi_id;
    pthread_create(&hmi_tid, NULL, hmi_thread_func, hmi_thread_data);

    print_safe("Main", "System is running. Simulating for 20 seconds.");
    print_safe("Main", "=============================================");

    // --- 步骤 5: 模拟动态变化 ---
    sleep(8);
    print_safe("Main", "=============================================");
    print_safe("Main", "!!! DYNAMIC CHANGE: Fan actuator hardware failure. Removing node.");
    ret = myconet_remove_node_name("actuator/fan");
    if (ret == MN_OK) {
        print_safe("Main", "Node 'actuator/fan' removed successfully.");
    }

    sleep(6);
    print_safe("Main", "=============================================");
    print_safe("Main", "!!! DYNAMIC CHANGE: HMI user closes temperature view.");
    ret = myconet_unsubscribe(hmi_id, "sensor/temp");
    if (ret == MN_OK) {
        print_safe("Main", "HMI unsubscribed from 'sensor/temp' successfully.");
    }

    // --- 步骤 6: 系统关闭 ---
    sleep(6);
    print_safe("Main", "=============================================");
    print_safe("Main", "--- System Shutting Down ---");
    g_system_running = false;

    pthread_join(sensor_tid, NULL);
    pthread_join(hmi_tid, NULL);
    print_safe("Main", "All threads joined.");

    myconet_deinit();
    pthread_mutex_destroy(&g_print_mutex);
    print_safe("Main", "MycoNet de-initialized. Exiting.");

    return 0;
}
