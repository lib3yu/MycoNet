// demo.cpp
#include "myconet.hpp"
#include <gtest/gtest.h>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <mutex>
#include <iomanip>
#include <string.h>

using namespace MycoNets;

// ====================================================================
// 1. 全局控制与统计
// ====================================================================
std::atomic<bool> is_running = true;
std::mutex print_mutex; // 用于保护 std::cout 的输出

// 统计信息
std::atomic<int> temp_msgs_sent = 0;
std::atomic<int> pressure_msgs_sent = 0;
std::atomic<int> commands_processed = 0;
std::atomic<int> hmi_notifications_sent = 0;
std::atomic<int> clu_status_updates = 0;

// 辅助打印函数，确保多线程输出不混乱
template<typename... Args>
void safe_print(Args&&... args) {
    std::lock_guard<std::mutex> lock(print_mutex);
    (std::cout << ... << args) << std::endl;
}

// ====================================================================
// 2. 数据结构定义
// ====================================================================
struct SensorData {
    double value;
    long timestamp;
};

struct ActuatorCommand {
    char target_name[32];
    char command[32];
};

struct SystemConfig {
    double temperature_threshold;
    int pressure_limit;
};

struct CLUStatus {
    int active_sensors;
    bool is_critical;
};


// ====================================================================
// 3. 线程工作函数
// ====================================================================

/**
 * @brief 传感器线程逻辑
 * @param node 传感器节点
 * @param initial_delay 初始延迟，用于错开启动时间
 */
void sensor_thread_func(std::shared_ptr<MycoNode> node, std::chrono::milliseconds initial_delay) {
    std::this_thread::sleep_for(initial_delay);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(50, 150); // 随机发布间隔
    std::uniform_real_distribution<> value_distrib(20.0, 110.0);

    while (is_running) {
        SensorData data = {
            value_distrib(gen),
            std::chrono::system_clock::now().time_since_epoch().count()
        };
        node->Publish(&data, sizeof(data));

        if (node->node_name.find("temp") != std::string::npos) {
            temp_msgs_sent++;
        } else {
            pressure_msgs_sent++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(distrib(gen)));
    }
}

/**
 * @brief 执行器线程逻辑
 * @param node 执行器节点
 */
void actuator_thread_func(std::shared_ptr<MycoNode> node) {
    // 执行器是被动接收命令的，主循环可以保持空闲或执行其他任务
    while (is_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    safe_print("[", node->node_name, "] Shutting down.");
}

/**
 * @brief HMI 线程逻辑
 * @param node HMI 节点
 */
void hmi_thread_func(std::shared_ptr<MycoNode> node) {
    while (is_running) {
        // 模拟用户每 2 秒发送一次高优先级通知
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        const char* notification = "MANUAL_CHECK_REQUESTED";
        int result = node->Notify("clu/main", notification, strlen(notification) + 1);
        if (result == MN_OK) {
            hmi_notifications_sent++;
        }
    }
}

/**
 * @brief CLU 线程逻辑
 * @param node CLU 节点
 * @param status_node CLU 的状态发布节点
 */
void clu_thread_func(std::shared_ptr<MycoNode> node, std::shared_ptr<MycoNode> status_node) {
    // 1. 从配置服务中 Pull 配置
    SystemConfig config;
    safe_print("[CLU] Attempting to pull configuration...");
    int pull_result = node->Pull("config/system", &config, sizeof(config));
    if (pull_result == MN_OK) {
        safe_print("[CLU] Configuration pulled successfully: Temp Threshold = ", config.temperature_threshold);
    } else {
        safe_print("[CLU] Failed to pull config, using defaults. Error: ", MycoNet::StrErrCode(pull_result));
        config.temperature_threshold = 90.0; // 默认值
    }

    // 2. 主循环，处理数据和发布状态
    while (is_running) {
        // 模拟 CLU 的工作周期
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 发布自己的状态 (LATCHED)
        CLUStatus status = {node->PubNum(), false}; // PubNum() 是订阅了本节点的数量
        status_node->Publish(&status, sizeof(status));
        clu_status_updates++;
    }
}


// ====================================================================
// 4. 主函数
// ====================================================================
int main() {
    const int NUM_TEMP_SENSORS = 10;
    const int NUM_PRESSURE_SENSORS = 10;
    const int NUM_ACTUATORS = 3;
    const int TOTAL_SENSORS = NUM_TEMP_SENSORS + NUM_PRESSURE_SENSORS;
    const auto RUN_DURATION = std::chrono::seconds(20);

    MycoNet& net = MycoNet::Inst();
    std::vector<std::thread> threads;

    safe_print("========== MycoNet Industrial Control Demo ==========");
    safe_print("Simulating for ", RUN_DURATION.count(), " seconds with ", 
               TOTAL_SENSORS + NUM_ACTUATORS + 2, " threads.");
    safe_print("=====================================================");

    // --- 1. 创建配置服务节点 (无线程) ---
    NodeParam config_param = {};
    config_param.size = sizeof(SystemConfig);
    config_param.conflags = (NodeFlag)(CONF_CACHED); // 启用缓存，用于 Pull
    auto config_node = net.NewNode("config/system", config_param);
    
    // 填充配置数据
    SystemConfig initial_config = {95.0, 200};
    // 直接发布到自己的缓存中
    config_node->Publish(&initial_config, sizeof(initial_config));
    safe_print("[SETUP] Config node 'config/system' created and populated.");

    // --- 2. 创建执行器节点和线程 ---
    // 注意：我们特意先创建一个执行器，让它去订阅还不存在的 CLU 节点，以测试延迟订阅
    std::vector<std::shared_ptr<MycoNode>> actuators;
    
    // 执行器回调函数
    auto actuator_cb = [&](const EventParam* param) {
        if (param->event == EVENT_PUBLISH) {
            ActuatorCommand* cmd = (ActuatorCommand*)param->data_p;
            auto node = net.GetNode(param->recver);
            if (node && strcmp(cmd->target_name, node->node_name.c_str()) == 0) {
                safe_print("[ACTUATOR] ", node->node_name, " received command '", cmd->command, "' from CLU");
                commands_processed++;
            }
        }
    };

    for (int i = 0; i < NUM_ACTUATORS; ++i) {
        std::string name = "actuator/" + std::to_string(i);
        NodeParam p = {};
        p.event_msk = EVENT_PUBLISH;
        p.event_cb = actuator_cb;
        auto node = net.NewNode(name, p);
        actuators.push_back(node);
        node->Subscribe("clu/main"); // 订阅 CLU 的指令
        safe_print("[SETUP] Actuator '", name, "' created and subscribing to 'clu/main'.");
        threads.emplace_back(actuator_thread_func, node);
    }

    // --- 3. 创建 CLU (中央控制逻辑单元) ---
    double latest_temp = 0.0;
    auto clu_cb = [&](const EventParam* param) {
        if (param->event == EVENT_PUBLISH) {
            // 简单处理：只关心温度
            auto sender_node = net.GetNode(param->sender);
            if (sender_node && sender_node->node_name.find("temp") != std::string::npos) {
                SensorData* data = (SensorData*)param->data_p;
                latest_temp = data->value; // 更新最新温度
            }
        } else if (param->event == EVENT_NOTIFY) {
            safe_print("[CLU] Received NOTIFY from HMI: ", (char*)param->data_p);
        }
    };
    
    NodeParam clu_param = {};
    clu_param.event_msk = (EventMask)(EVENT_PUBLISH | EVENT_NOTIFY);
    clu_param.event_cb = clu_cb;
    auto clu_node = net.NewNode("clu/main", clu_param);
    safe_print("[SETUP] CLU node 'clu/main' created. Pending subscriptions should now resolve.");

    // CLU 的状态发布节点 (LATCHED)
    NodeParam status_param = {};
    status_param.size = sizeof(CLUStatus);
    status_param.conflags = (NodeFlag)(CONF_CACHED | CONF_LATCHED);
    auto clu_status_node = net.NewNode("clu/status", status_param);
    
    threads.emplace_back(clu_thread_func, clu_node, clu_status_node);

    // CLU 决策逻辑（在主线程中模拟，也可以放在 CLU 线程中）
    // 为了简化，我们让 CLU 的决策逻辑在另一个线程中运行
    threads.emplace_back([&]() {
        while(is_running) {
            if (latest_temp > initial_config.temperature_threshold) {
                ActuatorCommand cmd = {};
                strcpy(cmd.target_name, "actuator/2"); // 命令冷却器
                strcpy(cmd.command, "START_COOLING");
                clu_node->Publish(&cmd, sizeof(cmd));
                safe_print("[CLU] High temperature detected (", std::fixed, std::setprecision(1), latest_temp, "°C)! Commanding 'actuator/2' to cool down.");
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });


    // --- 4. 创建 HMI ---
    auto hmi_cb = [&](const EventParam* param) {
        if (param->event == EVENT_PUBLISH || param->event == EVENT_LATCHED) {
            CLUStatus* status = (CLUStatus*)param->data_p;
            safe_print("[HMI] CLU Status Update (", (param->event == EVENT_LATCHED ? "LATCHED" : "LIVE"),
                       "): Active Sensors=", status->active_sensors, 
                       ", Critical=", (status->is_critical ? "YES" : "NO"));
        }
    };
    NodeParam hmi_param = {};
    hmi_param.event_msk = (EventMask)(EVENT_PUBLISH | EVENT_LATCHED);
    hmi_param.event_cb = hmi_cb;
    auto hmi_node = net.NewNode("hmi/panel", hmi_param);
    hmi_node->Subscribe("clu/status");
    threads.emplace_back(hmi_thread_func, hmi_node);
    safe_print("[SETUP] HMI node 'hmi/panel' created and subscribed to 'clu/status'.");

    // --- 5. 创建传感器节点和线程 ---
    NodeParam sensor_param = {}; // 所有传感器使用相同配置
    sensor_param.size = sizeof(SensorData);
    sensor_param.conflags = CONF_CACHED; // 缓存最新数据

    for (int i = 0; i < NUM_TEMP_SENSORS; ++i) {
        std::string name = "sensor/temp/" + std::to_string(i);
        auto node = net.NewNode(name, sensor_param);
        clu_node->Subscribe(name); // CLU 订阅此传感器
        safe_print("[SETUP] Node ", name, " created and attempting to subscribe to 'clu/main'.");
        threads.emplace_back(sensor_thread_func, node, std::chrono::milliseconds(i * 10));
    }
    for (int i = 0; i < NUM_PRESSURE_SENSORS; ++i) {
        std::string name = "sensor/pressure/" + std::to_string(i);
        auto node = net.NewNode(name, sensor_param);
        clu_node->Subscribe(name); // CLU 订阅此传感器
        safe_print("[SETUP] Node ", name, " created and attempting to subscribe to 'clu/main'.");
        threads.emplace_back(sensor_thread_func, node, std::chrono::milliseconds(i * 10));
    }
    safe_print("[SETUP] All ", TOTAL_SENSORS, " sensor nodes and threads created.");

    // --- 6. 运行并等待 ---
    safe_print("\n[SYSTEM] Simulation running... Press Ctrl+C to exit early.\n");
    std::this_thread::sleep_for(RUN_DURATION);
    is_running = false;

    // --- 7. 清理和报告 ---
    safe_print("\n[SYSTEM] Simulation time ended. Shutting down all threads...");
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    safe_print("\n========== Final Statistics ==========");
    safe_print("Total Temperature Messages Sent: ", temp_msgs_sent.load());
    safe_print("Total Pressure Messages Sent:    ", pressure_msgs_sent.load());
    safe_print("Total Actuator Commands Processed: ", commands_processed.load());
    safe_print("Total HMI Notifications Sent:      ", hmi_notifications_sent.load());
    safe_print("Total CLU Status Updates:          ", clu_status_updates.load());
    safe_print("Final Node Count in Network:       ", net.NodeNum());
    safe_print("======================================");
    safe_print("Demo finished.");

    return 0;
}