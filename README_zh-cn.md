# MycoNet

**MycoNet** 是一个轻量级、线程安全的进程内消息传递库，适用于 C 和 C++。它提供了一个灵活的通信框架，基于发布/订阅模式，同时支持直接的请求-响应（Pull）和单向通知（Notify）模型。其解耦的、事件驱动的架构使其成为构建模块化和可扩展应用程序的理想选择。

## 核心概念

-   **MycoNet 实例**: 一个中心的、类似单例的管理器，负责监督所有节点。它处理节点的创建、删除和查找。
-   **MycoNode (节点)**: 基本的通信端点。一个节点可以作为发布者、订阅者，或两者兼备。每个节点都由一个唯一的名称和一个在运行时分配的 ID 来标识。
-   **通信模式**:
    -   **发布/订阅 (Publish/Subscribe)**: 一个节点将数据发布到一个主题（即它自身），所有订阅了该节点的其他节点都会收到数据。这是一种一对多的模式。
    -   **拉取 (Pull, 请求/响应)**: 一个节点直接从另一个特定节点请求数据。这是一种一对一的模式。
    -   **通知 (Notify)**: 一个节点向另一个特定节点发送一个直接的、单向的消息，无需订阅关系。

## 主要特性

-   **线程安全**: 从底层设计上就为多线程环境而生。对网络和节点的所有操作都由互斥锁保护，允许安全的并发访问。
-   **双 API**: 同时提供现代 C++ API（使用 `std::shared_ptr`, `std::function` 等）和纯 C API，以实现最大程度的兼容性。
-   **事件驱动**: 节点的逻辑通过事件回调函数来实现。节点可以使用事件掩码来订阅特定的事件，如 `EVENT_PUBLISH`、`EVENT_PULL` 等。
-   **数据缓存**: 节点可以配置一个数据缓存。当发布数据时，数据会存储在缓存中。这对于 `Pull` 操作非常高效。
-   **闩锁 (Latching)**: 发布者的一项强大功能。当一个新节点订阅一个"闩锁"的发布者时，它会立即收到最后缓存的消息，这对于获取初始状态非常有用。这会触发订阅者的 `EVENT_LATCHED` 事件。
-   **挂起订阅 (Pending Subscriptions)**: 如果一个节点尝试订阅一个不存在的节点，该请求会被放入队列。一旦目标节点被创建，订阅就会自动完成，从而解除了节点间的初始化顺序依赖。

## 用法与示例

让我们演示一个包含三个节点的场景：
1.  `SensorNode` (传感器节点): 定期发布温度数据。它启用了缓存和闩锁功能。
2.  `LoggerNode` (日志节点): 订阅 `SensorNode`，并打印它收到的任何温度数据。
3.  `ControllerNode` (控制器节点): 可以按需从 `SensorNode` 拉取当前温度，也可以向其发送“重新校准”的通知。

### C++ API 示例

```cpp
#include "myconet.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace MycoNets;

// 日志节点和传感器节点的事件回调函数
void logger_sensor_cb(const EventParam* param) {
    switch (param->event) {
        case EVENT_PUBLISH:
        case EVENT_LATCHED: {
            float temp = *(static_cast<float*>(param->data_p));
            std::cout << "[日志节点] 收到温度: " << temp 
                      << " 来自传感器 (ID: " << param->sender << "). 事件: "
                      << ((param->event == EVENT_LATCHED) ? "LATCHED" : "PUBLISH") << std::endl;
            break;
        }
        case EVENT_NOTIFY: {
             const char* msg = static_cast<const char*>(param->data_p);
             std::cout << "[传感器节点] 收到来自控制器 (ID: " << param->sender 
                       << ") 的通知: " << msg << std::endl;
            break;
        }
        default:
            break;
    }
}

int main() {
    // 获取默认的 MycoNet 实例
    auto& net = MycoNet::Inst();

    // 1. 创建 SensorNode，启用缓存和闩锁
    NodeParam sensor_param = {};
    sensor_param.size = sizeof(float); // 缓存大小
    sensor_param.conflags = (NodeFlag)(CONF_CACHED | CONF_LATCHED);
    sensor_param.event_msk = EVENT_NOTIFY; // 关心通知事件
    sensor_param.event_cb = logger_sensor_cb;
    auto sensor_node = net.NewNode("SensorNode", sensor_param);
    if (!sensor_node) {
        std::cerr << "创建 SensorNode 失败" << std::endl;
        return -1;
    }

    // 2. 创建 LoggerNode
    NodeParam logger_param = {};
    logger_param.event_msk = (EventMask)(EVENT_PUBLISH | EVENT_LATCHED); // 关心发布和闩锁事件
    logger_param.event_cb = logger_sensor_cb;
    auto logger_node = net.NewNode("LoggerNode", logger_param);

    // 3. 创建 ControllerNode (本示例中不需要回调)
    NodeParam controller_param = {};
    auto controller_node = net.NewNode("ControllerNode", controller_param);

    // 4. LoggerNode 订阅 SensorNode。如果 SensorNode 有缓存数据，它会立即收到。
    // (此时传感器还未发布，所以没有闩锁消息)
    logger_node->Subscribe("SensorNode");

    // 5. SensorNode 发布它的第一个温度读数
    float current_temp = 25.3f;
    std::cout << "[传感器节点] 发布温度: " << current_temp << std::endl;
    sensor_node->Publish(&current_temp, sizeof(current_temp));

    // 创建第二个日志节点来演示闩锁功能
    auto logger_node2 = net.NewNode("LoggerNode2", logger_param);
    std::cout << "\n[Main] LoggerNode2 正在订阅..." << std::endl;
    logger_node2->Subscribe("SensorNode"); // 这将立即触发 LATCHED 事件

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 6. ControllerNode 从传感器拉取最新数据
    float pulled_temp = 0.0f;
    int pull_res = controller_node->Pull("SensorNode", &pulled_temp, sizeof(pulled_temp));
    if (pull_res >= 0) {
        std::cout << "\n[控制器节点] 成功拉取温度: " << pulled_temp << std::endl;
    }

    // 7. ControllerNode 通知传感器进行重新校准
    const char* notify_msg = "RECALIBRATE";
    std::cout << "[控制器节点] 通知 SensorNode: '" << notify_msg << "'" << std::endl;
    controller_node->Notify("SensorNode", notify_msg, strlen(notify_msg) + 1);
    
    return 0;
}
```

### C API 示例

```c
#include "myconet.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h> // for sleep

// 日志节点和传感器节点的事件回调函数
void logger_sensor_c_cb(const MycoNet_EventParam_t* param) {
    switch (param->event) {
        case EVENT_PUBLISH:
        case EVENT_LATCHED: {
            float temp = *(float*)(param->data_p);
            printf("[日志节点] 收到温度: %.1f 来自传感器 (ID: %u). 事件: %s\n",
                   temp, param->sender, (param->event == EVENT_LATCHED) ? "LATCHED" : "PUBLISH");
            break;
        }
        case EVENT_NOTIFY: {
             const char* msg = (const char*)(param->data_p);
             printf("[传感器节点] 收到来自控制器 (ID: %u) 的通知: %s\n",
                    param->sender, msg);
            break;
        }
        default:
            break;
    }
}

int main() {
    // 初始化默认的 MycoNet 实例
    myconet_init();

    MycoNet_ID_t sensor_id, logger_id, controller_id, logger_id2;

    // 1. 创建 SensorNode，启用缓存和闩锁
    MycoNet_NodeParam_t sensor_param = {0};
    sensor_param.size = sizeof(float); // 缓存大小
    sensor_param.conflags = CONF_CACHED | CONF_LATCHED;
    sensor_param.event_msk = EVENT_NOTIFY; // 关心通知事件
    sensor_param.event_cb = logger_sensor_c_cb;
    myconet_create_node(&sensor_id, "SensorNode", &sensor_param);

    // 2. 创建 LoggerNode
    MycoNet_NodeParam_t logger_param = {0};
    logger_param.event_msk = EVENT_PUBLISH | EVENT_LATCHED; // 关心发布和闩锁事件
    logger_param.event_cb = logger_sensor_c_cb;
    myconet_create_node(&logger_id, "LoggerNode", &logger_param);

    // 3. 创建 ControllerNode
    MycoNet_NodeParam_t controller_param = {0};
    myconet_create_node(&controller_id, "ControllerNode", &controller_param);

    // 4. LoggerNode 订阅 SensorNode
    myconet_subscribe(logger_id, "SensorNode");

    // 5. SensorNode 发布它的第一个温度读数
    float current_temp = 25.3f;
    printf("[传感器节点] 发布温度: %.1f\n", current_temp);
    myconet_publish(sensor_id, &current_temp, sizeof(current_temp));

    // 演示闩锁功能
    printf("\n[Main] LoggerNode2 正在订阅...\n");
    myconet_create_node(&logger_id2, "LoggerNode2", &logger_param);
    myconet_subscribe(logger_id2, "SensorNode"); // 这将立即触发 LATCHED 事件

    sleep(1);

    // 6. ControllerNode 从传感器拉取最新数据
    float pulled_temp = 0.0f;
    int pull_res = myconet_pull(controller_id, "SensorNode", &pulled_temp, sizeof(pulled_temp));
    if (pull_res >= 0) {
        printf("\n[控制器节点] 成功拉取温度: %.1f\n", pulled_temp);
    }

    // 7. ControllerNode 通知传感器进行重新校准
    const char* notify_msg = "RECALIBRATE";
    printf("[控制器节点] 通知 SensorNode: '%s'\n", notify_msg);
    myconet_notify(controller_id, "SensorNode", notify_msg, strlen(notify_msg) + 1);

    // 清理资源
    myconet_deinit();
    
    return 0;
}
```

..
