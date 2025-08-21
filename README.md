# MycoNet

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/eleven-3/MycoNet)

[简体中文](README_zh-cn.md)

--- 

**MycoNet** is a lightweight, thread-safe, in-process messaging library for C and C++. It provides a flexible communication framework based on the publish-subscribe pattern, also supporting direct request-response (Pull) and one-way notification (Notify) models. Its decoupled, event-driven architecture makes it ideal for building modular and scalable applications.

## Core Concepts

-   **MycoNet Instance**: A central, singleton-like manager that oversees all nodes. It handles node creation, deletion, and lookup.
-   **MycoNode**: The fundamental communication endpoint. A node can act as a publisher, a subscriber, or both. Each node is identified by a unique name and a runtime-assigned ID.
-   **Communication Patterns**:
    -   **Publish/Subscribe**: A node publishes data to a topic (itself), and all subscribed nodes receive the data. This is a one-to-many pattern.
    -   **Pull (Request/Response)**: A node directly requests data from another specific node. This is a one-to-one pattern.
    -   **Notify**: A node sends a direct, one-way message to another specific node without requiring a subscription.

## Key Features

-   **Thread-Safe**: Designed from the ground up for multi-threaded environments. All operations on the network and nodes are protected by mutexes, allowing safe concurrent access.
-   **Dual API**: Offers both a modern C++ API (using `std::shared_ptr`, `std::function`, etc.) and a pure C API for maximum compatibility.
-   **Event-Driven**: Node logic is implemented via event callbacks. Nodes can subscribe to specific events like `EVENT_PUBLISH`, `EVENT_PULL`, etc., using an event mask.
-   **Data Caching**: Nodes can be configured with a data cache. When publishing, the data is stored in the cache. This is highly efficient for `Pull` operations.
-   **Latching**: A powerful feature for publishers. When a new node subscribes to a "latched" publisher, it immediately receives the last cached message, which is perfect for getting initial state.
-   **Pending Subscriptions**: If a node tries to subscribe to a non-existent node, the request is queued. The subscription is automatically completed once the target node is created, decoupling initialization order.

## Usage & Examples

Let's demonstrate a scenario with three nodes:
1.  `SensorNode`: Publishes temperature data periodically. It has caching and latching enabled.
2.  `LoggerNode`: Subscribes to `SensorNode` and prints any temperature data it receives.
3.  `ControllerNode`: Can pull the current temperature from `SensorNode` on demand and can also send a "recalibrate" notification to it.

### C++ API Example

```cpp
#include "myconet.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace MycoNets;

// Event callback for the Logger and Sensor nodes
void logger_sensor_cb(const EventParam* param) {
    switch (param->event) {
        case EVENT_PUBLISH:
        case EVENT_LATCHED: {
            float temp = *(static_cast<float*>(param->data_p));
            std::cout << "[Logger] Received temperature: " << temp 
                      << " from Sensor (ID: " << param->sender << "). Event: "
                      << ((param->event == EVENT_LATCHED) ? "LATCHED" : "PUBLISH") << std::endl;
            break;
        }
        case EVENT_NOTIFY: {
             const char* msg = static_cast<const char*>(param->data_p);
             std::cout << "[Sensor] Received notification from Controller (ID: " << param->sender 
                       << "): " << msg << std::endl;
            break;
        }
        default:
            break;
    }
}

int main() {
    // Get the default MycoNet instance
    auto& net = MycoNet::Inst();

    // 1. Create SensorNode with caching and latching
    NodeParam sensor_param = {};
    sensor_param.size = sizeof(float); // Cache size
    sensor_param.conflags = (NodeFlag)(CONF_CACHED | CONF_LATCHED);
    sensor_param.event_msk = EVENT_NOTIFY; // Interested in notify events
    sensor_param.event_cb = logger_sensor_cb;
    auto sensor_node = net.NewNode("SensorNode", sensor_param);
    if (!sensor_node) {
        std::cerr << "Failed to create SensorNode" << std::endl;
        return -1;
    }

    // 2. Create LoggerNode
    NodeParam logger_param = {};
    logger_param.event_msk = (EventMask)(EVENT_PUBLISH | EVENT_LATCHED); // Interested in publish and latched events
    logger_param.event_cb = logger_sensor_cb;
    auto logger_node = net.NewNode("LoggerNode", logger_param);

    // 3. Create ControllerNode (no callback needed for this example)
    NodeParam controller_param = {};
    auto controller_node = net.NewNode("ControllerNode", controller_param);

    // 4. Logger subscribes to Sensor. It will immediately get the latched message if available.
    // (Sensor hasn't published yet, so no latched message for now)
    logger_node->Subscribe("SensorNode");

    // 5. Sensor publishes its first temperature reading
    float current_temp = 25.3f;
    std::cout << "[Sensor] Publishing temperature: " << current_temp << std::endl;
    sensor_node->Publish(&current_temp, sizeof(current_temp));

    // Let's create a second logger to demonstrate latching
    auto logger_node2 = net.NewNode("LoggerNode2", logger_param);
    std::cout << "\n[Main] LoggerNode2 is subscribing now..." << std::endl;
    logger_node2->Subscribe("SensorNode"); // This will trigger the LATCHED event immediately

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 6. Controller pulls the latest data from the sensor
    float pulled_temp = 0.0f;
    int pull_res = controller_node->Pull("SensorNode", &pulled_temp, sizeof(pulled_temp));
    if (pull_res >= 0) {
        std::cout << "\n[Controller] Pulled temperature successfully: " << pulled_temp << std::endl;
    }

    // 7. Controller notifies the sensor to recalibrate
    const char* notify_msg = "RECALIBRATE";
    std::cout << "[Controller] Notifying SensorNode to '" << notify_msg << "'" << std::endl;
    controller_node->Notify("SensorNode", notify_msg, strlen(notify_msg) + 1);
    
    return 0;
}
```

### C API Example

```c
#include "myconet.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h> // for sleep

// Event callback for the Logger and Sensor nodes
void logger_sensor_c_cb(const MycoNet_EventParam_t* param) {
    switch (param->event) {
        case EVENT_PUBLISH:
        case EVENT_LATCHED: {
            float temp = *(float*)(param->data_p);
            printf("[Logger] Received temperature: %.1f from Sensor (ID: %u). Event: %s\n",
                   temp, param->sender, (param->event == EVENT_LATCHED) ? "LATCHED" : "PUBLISH");
            break;
        }
        case EVENT_NOTIFY: {
             const char* msg = (const char*)(param->data_p);
             printf("[Sensor] Received notification from Controller (ID: %u): %s\n",
                    param->sender, msg);
            break;
        }
        default:
            break;
    }
}

int main() {
    // Initialize the default MycoNet instance
    myconet_init();

    MycoNet_ID_t sensor_id, logger_id, controller_id, logger_id2;

    // 1. Create SensorNode with caching and latching
    MycoNet_NodeParam_t sensor_param = {0};
    sensor_param.size = sizeof(float); // Cache size
    sensor_param.conflags = CONF_CACHED | CONF_LATCHED;
    sensor_param.event_msk = EVENT_NOTIFY; // Interested in notify events
    sensor_param.event_cb = logger_sensor_c_cb;
    myconet_create_node(&sensor_id, "SensorNode", &sensor_param);

    // 2. Create LoggerNode
    MycoNet_NodeParam_t logger_param = {0};
    logger_param.event_msk = EVENT_PUBLISH | EVENT_LATCHED; // Interested in publish and latched events
    logger_param.event_cb = logger_sensor_c_cb;
    myconet_create_node(&logger_id, "LoggerNode", &logger_param);

    // 3. Create ControllerNode
    MycoNet_NodeParam_t controller_param = {0};
    myconet_create_node(&controller_id, "ControllerNode", &controller_param);

    // 4. Logger subscribes to Sensor
    myconet_subscribe(logger_id, "SensorNode");

    // 5. Sensor publishes its first temperature reading
    float current_temp = 25.3f;
    printf("[Sensor] Publishing temperature: %.1f\n", current_temp);
    myconet_publish(sensor_id, &current_temp, sizeof(current_temp));

    // Demonstrate latching
    printf("\n[Main] LoggerNode2 is subscribing now...\n");
    myconet_create_node(&logger_id2, "LoggerNode2", &logger_param);
    myconet_subscribe(logger_id2, "SensorNode"); // This will trigger the LATCHED event

    sleep(1);

    // 6. Controller pulls the latest data from the sensor
    float pulled_temp = 0.0f;
    int pull_res = myconet_pull(controller_id, "SensorNode", &pulled_temp, sizeof(pulled_temp));
    if (pull_res >= 0) {
        printf("\n[Controller] Pulled temperature successfully: %.1f\n", pulled_temp);
    }

    // 7. Controller notifies the sensor to recalibrate
    const char* notify_msg = "RECALIBRATE";
    printf("[Controller] Notifying SensorNode to '%s'\n", notify_msg);
    myconet_notify(controller_id, "SensorNode", notify_msg, strlen(notify_msg) + 1);

    // Clean up
    myconet_deinit();
    
    return 0;
}
```

...
