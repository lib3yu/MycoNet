#include "myconet.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <random>
#include <iomanip>

using namespace MycoNets;

// 使用一个全局互斥锁来保护cout，防止多线程输出混乱
std::mutex g_cout_mutex;

// ====================================================================
// 1. 消息数据结构定义
// ====================================================================
struct TemperatureData {
    double temperature;
    std::chrono::system_clock::time_point timestamp;
};

struct FanCommand {
    bool turn_on;
    int fan_speed; // 0-100%
};

// ====================================================================
// 2. 辅助函数和全局控制变量
// ====================================================================
std::atomic<bool> g_system_running(true);

// 线程安全的打印函数
template<typename... Args>
void print_safe(const std::string& source, Args... args) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << "[" << std::setw(20) << std::left << source << "] ";
    (std::cout << ... << args) << std::endl;
}

// ====================================================================
// 3. 节点回调函数
// ====================================================================

// 控制器的事件回调
void controller_event_cb(const EventParam* param) {
    // 假设控制器只订阅了温度传感器
    if (param->event == EVENT_PUBLISH && param->data_p) {
        auto* data = static_cast<const TemperatureData*>(param->data_p);
        print_safe("CoolingController", "Received temperature: ", data->temperature, " C");

        // 控制逻辑：温度高于28度则开启风扇
        if (data->temperature > 28.0) {
            // 获取与此回调关联的控制器节点
            auto controller_node = MycoNet::Self().GetNode(param->recver);
            if (controller_node) {
                FanCommand cmd = {true, 80};
                print_safe("CoolingController", "Temp HIGH! Publishing FAN ON command.");
                controller_node->Publish(&cmd, sizeof(cmd));
            }
        }
    }
    // 处理来自HMI的紧急通知
    else if (param->event == EVENT_NOTIFY && param->data_p) {
        auto* cmd = static_cast<const FanCommand*>(param->data_p);
        if (cmd->turn_on) {
            print_safe("CoolingController", "Received URGENT NOTIFY from HMI. Forcing FAN ON.");
            auto controller_node = MycoNet::Self().GetNode(param->recver);
            if (controller_node) {
                controller_node->Publish(cmd, sizeof(*cmd));
            }
        }
    }
}

// 风扇执行器的事件回调
void fan_event_cb(const EventParam* param) {
    if (param->event == EVENT_PUBLISH && param->data_p) {
        auto* cmd = static_cast<const FanCommand*>(param->data_p);
        if (cmd->turn_on) {
            print_safe("FanActuator", "Command received. Turning ON fan at speed ", cmd->fan_speed, "%");
        } else {
            print_safe("FanActuator", "Command received. Turning OFF fan.");
        }
    }
}

// HMI的事件回调
void hmi_event_cb(const EventParam* param) {
    if (param->event == EVENT_PUBLISH || param->event == EVENT_LATCHED) {
        // 通过发送方ID判断消息来源
        auto sender_node = MycoNet::Self().GetNode(param->sender);
        if (!sender_node) return;

        if (sender_node->node_name == "sensor/temp") {
            auto* data = static_cast<const TemperatureData*>(param->data_p);
            print_safe("HMI", "MONITOR: Current Temperature is ", data->temperature, " C. (Event: ", (param->event == EVENT_LATCHED ? "LATCHED" : "PUBLISH"), ")");
        } else if (sender_node->node_name == "actuator/fan") {
            // HMI 也可以订阅执行器状态，这里简化为订阅控制器指令
        }
    }
}


// ====================================================================
// 4. 节点线程函数
// ====================================================================

void sensor_thread_func(std::shared_ptr<MycoNode> node) {
    std::default_random_engine generator;
    std::uniform_real_distribution<double> distribution(20.0, 35.0);
    
    while (g_system_running) {
        TemperatureData data;
        data.temperature = distribution(generator);
        data.timestamp = std::chrono::system_clock::now();

        print_safe(node->node_name, "Publishing temperature: ", data.temperature, " C");
        node->Publish(&data, sizeof(data));
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    print_safe(node->node_name, "Thread shutting down.");
}

void hmi_thread_func(std::shared_ptr<MycoNode> node) {
    // 1. 启动后立即PULL一次传感器的初始值，演示LATCHED数据的获取
    TemperatureData initial_temp;
    print_safe(node->node_name, "Attempting to PULL initial temperature...");
    int ret = node->Pull("sensor/temp", &initial_temp, sizeof(initial_temp));
    if (ret == MN_OK || ret == MN_INFO_CACHE_PULLED) {
        print_safe(node->node_name, "PULL successful! Initial temperature: ", initial_temp.temperature, " C");
    } else {
        print_safe(node->node_name, "PULL failed. Error: ", MycoNet::StrErrCode(ret), ". Will rely on subscription.");
    }

    // 2. 模拟HMI在5秒后发送一个紧急通知
    std::this_thread::sleep_for(std::chrono::seconds(5));
    print_safe(node->node_name, "Sending urgent NOTIFY to controller.");
    FanCommand urgent_cmd = {true, 100};
    node->Notify("controller/cooling", &urgent_cmd, sizeof(urgent_cmd));

    // 3. HMI线程继续运行，接收订阅的消息
    while(g_system_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    print_safe(node->node_name, "Thread shutting down.");
}


// ====================================================================
// 5. 主函数 - 系统编排
// ====================================================================

int main() {
    auto& net = MycoNet::Self();
    std::vector<std::thread> threads;

    // --- 步骤 1: 创建初始节点 ---
    print_safe("Main", "--- System Initializing ---");

    // 创建温度传感器节点 (带缓存)
    NodeParam sensor_param = {};
    sensor_param.size = sizeof(TemperatureData);
    sensor_param.conflags = CONF_CACHED; // 启用缓存
    auto sensor_node = net.NewNode("sensor/temp", sensor_param);

    // 创建控制器节点
    NodeParam controller_param = {};
    controller_param.event_msk = EVENT_PUBLISH | EVENT_NOTIFY;
    controller_param.event_cb = controller_event_cb;
    auto controller_node = net.NewNode("controller/cooling", controller_param);

    // 创建风扇执行器节点
    NodeParam fan_param = {};
    fan_param.event_msk = EVENT_PUBLISH;
    fan_param.event_cb = fan_event_cb;
    auto fan_node = net.NewNode("actuator/fan", fan_param);

    // 创建HMI节点
    NodeParam hmi_param = {};
    hmi_param.event_msk = EVENT_PUBLISH | EVENT_LATCHED;
    hmi_param.event_cb = hmi_event_cb;
    auto hmi_node = net.NewNode("hmi/monitor", hmi_param);

    // --- 步骤 2: 建立订阅关系 ---
    print_safe("Main", "--- Establishing Subscriptions ---");
    controller_node->Subscribe("sensor/temp");
    fan_node->Subscribe("controller/cooling");
    hmi_node->Subscribe("sensor/temp");

    // --- 步骤 3: 启动节点线程 ---
    print_safe("Main", "--- Starting Node Threads ---");
    threads.emplace_back(sensor_thread_func, sensor_node);
    threads.emplace_back(hmi_thread_func, hmi_node);
    // 控制器和执行器是被动节点，不需要自己的线程，它们的行为由回调函数驱动

    print_safe("Main", "System is running. Simulating for 20 seconds.");
    print_safe("Main", "=============================================");

    // --- 步骤 4: 模拟动态变化 ---
    std::this_thread::sleep_for(std::chrono::seconds(8));
    print_safe("Main", "=============================================");
    print_safe("Main", "!!! DYNAMIC CHANGE: Fan actuator hardware failure. Removing node.");
    // 动态移除节点
    net.RemoveNode(fan_node->MyID());
    print_safe("Main", "Node 'actuator/fan' removed.");
    print_safe("Main", "=============================================");


    std::this_thread::sleep_for(std::chrono::seconds(6));
    print_safe("Main", "=============================================");
    print_safe("Main", "!!! DYNAMIC CHANGE: HMI user closes temperature view.");
    // 动态取消订阅
    hmi_node->Unsubscribe("sensor/temp");
    print_safe("Main", "HMI unsubscribed from 'sensor/temp'.");
    print_safe("Main", "=============================================");


    // --- 步骤 5: 系统关闭 ---
    std::this_thread::sleep_for(std::chrono::seconds(6));
    print_safe("Main", "=============================================");
    print_safe("Main", "--- System Shutting Down ---");
    g_system_running = false;

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    print_safe("Main", "All threads joined. Exiting.");
    return 0;
}
