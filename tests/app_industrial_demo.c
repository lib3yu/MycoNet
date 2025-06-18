#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "../src/DataHub.h"

typedef struct {
    double temperature;
    long timestamp;
} TempSensorData_t;

typedef struct {
    char command[64];
} ActuatorCommand_t;

int sensor_cb(DataNode_t* node, EventParam_t* param) {
    if (param->event == EVENT_PULL) {
        printf("[Sensor] >> PULL request received from '%s'.\n", param->sender->name);
    }
    return DH_OK;
}

int logger_cb(DataNode_t* node, EventParam_t* param) {
    if (param->event == EVENT_PUBLISH) {
        TempSensorData_t* data = (TempSensorData_t*)param->data_p;
        printf("[Logger] << Received temp data: %.2f C (from %s)\n", data->temperature, param->sender->name);
    }
    return DH_OK;
}

int actuator_cb(DataNode_t* node, EventParam_t* param) {
    if (param->event == EVENT_NOTIFY) {
        ActuatorCommand_t* cmd = (ActuatorCommand_t*)param->data_p;
        printf("[Actuator] << Received command: %s (from %s)\n", cmd->command, param->sender->name);
    }
    return DH_OK;
}

DataNode_t g_sensor_node = {
    .name = "temp_sensor_1",
    .size = sizeof(TempSensorData_t),
    .conflags = CONF_CACHED,
    .event_msk = EVENT_PULL,
    .event_cb = sensor_cb,
};

DataNode_t g_logger_node = {
    .name = "data_logger",
    .size = 0,
    .conflags = CONF_NONE,
    .event_msk = EVENT_PUBLISH,
    .event_cb = logger_cb,
};

DataNode_t g_control_node = {
    .name = "pid_controller",
    .size = 0,
    .conflags = CONF_NONE,
    .event_msk = EVENT_NONE,
    .event_cb = NULL,
};

DataNode_t g_actuator_node = {
    .name = "heater_actuator",
    .size = 0,
    .conflags = CONF_NONE,
    .event_msk = EVENT_NOTIFY,
    .event_cb = actuator_cb,
};

// ======================== Synchronization Primitive ========================

// A barrier to ensure all threads are ready before the main loop starts.
// We have 4 worker threads + 1 main thread.
#define NUM_APP_THREADS 5
static pthread_barrier_t g_init_barrier;


// ======================== Thread Functions (Modified) ========================

void* sensor_thread_func(void* arg) {
    printf("[Sensor] Thread started, waiting at barrier.\n");
    // Wait for all other threads to be ready before starting the main loop
    pthread_barrier_wait(&g_init_barrier);
    printf("[Sensor] Barrier passed, starting main loop.\n");

    double current_temp = 20.0;
    while (1) {
        TempSensorData_t data = { .temperature = current_temp, .timestamp = time(NULL) };
        printf("[Sensor] >> Publishing new temperature: %.2f C\n", current_temp);
        DataHub_NodePublish(&g_sensor_node, &data, sizeof(data));
        
        current_temp += (double)(rand() % 100 - 50) / 100.0;
        sleep(1);
    }
    return NULL;
}

void* logger_thread_func(void* arg) {
    printf("[Logger] Thread started. Subscribing to sensor.\n");
    // Subscription can happen immediately as it's part of its own setup
    DataHub_NodeSubscribe(&g_logger_node, g_sensor_node.name);
    
    printf("[Logger] Waiting at barrier.\n");
    pthread_barrier_wait(&g_init_barrier);
    printf("[Logger] Barrier passed.\n");

    // This thread now just waits. The callback does all the work.
    pthread_exit(NULL);
    return NULL;
}

void* control_thread_func(void* arg) {
    printf("[Control] Thread started, waiting at barrier.\n");
    pthread_barrier_wait(&g_init_barrier);
    printf("[Control] Barrier passed, starting main loop.\n");

    while (1) {
        sleep(5); 

        printf("[Control] Waking up to check temperature...\n");
        TempSensorData_t current_data;
        
        int ret = DataHub_NodePull(&g_control_node, g_sensor_node.name, &current_data, sizeof(current_data));

        if (ret == DH_OK) {
            printf("[Control] << Pulled temperature is %.2f C.\n", current_data.temperature);
            ActuatorCommand_t cmd;
            if (current_data.temperature < 22.0) {
                snprintf(cmd.command, sizeof(cmd.command), "TURN ON HEATER");
            } else if (current_data.temperature > 28.0) {
                snprintf(cmd.command, sizeof(cmd.command), "TURN OFF HEATER");
            } else {
                snprintf(cmd.command, sizeof(cmd.command), "MAINTAIN CURRENT STATE");
            }
            printf("[Control] >> Sending command to actuator: %s\n", cmd.command);
            DataHub_NodeNotify(&g_control_node, g_actuator_node.name, &cmd, sizeof(cmd));
        } else {
            fprintf(stderr, "[Control] !! Failed to pull data: %s\n", DataHub_GetErrStr(ret));
        }
    }
    return NULL;
}

void* actuator_thread_func(void* arg) {
    printf("[Actuator] Thread started, waiting at barrier.\n");
    pthread_barrier_wait(&g_init_barrier);
    printf("[Actuator] Barrier passed.\n");

    // This thread just waits. The callback does all the work.
    pthread_exit(NULL);
    return NULL;
}

// ======================== Main Function ========================

int main() {
    srand(time(NULL));
    printf("========== Industrial Demo Application ==========\n");

    // 1. Init Hub
    DataHub_Init();

    // 2. Init all nodes
    DataHub_InitNode(&g_sensor_node);
    DataHub_InitNode(&g_logger_node);
    DataHub_InitNode(&g_control_node);
    DataHub_InitNode(&g_actuator_node);

    // 3. Register all nodes to the hub
    DataHub_PushBackNode(&g_sensor_node);
    DataHub_PushBackNode(&g_logger_node);
    DataHub_PushBackNode(&g_control_node);
    DataHub_PushBackNode(&g_actuator_node);
    printf("All nodes initialized and registered by main thread.\n");

    // 4. Initialize the barrier
    pthread_barrier_init(&g_init_barrier, NULL, NUM_APP_THREADS);

    // 5. Create threads
    pthread_t sensor_tid, logger_tid, control_tid, actuator_tid;
    pthread_create(&sensor_tid, NULL, sensor_thread_func, NULL);
    pthread_create(&logger_tid, NULL, logger_thread_func, NULL);
    pthread_create(&control_tid, NULL, control_thread_func, NULL);
    pthread_create(&actuator_tid, NULL, actuator_thread_func, NULL);

    // 6. Main thread also waits at the barrier
    printf("[Main] Waiting at barrier for all threads to be ready...\n");
    pthread_barrier_wait(&g_init_barrier);
    printf("[Main] Barrier passed! System is running.\n");

    // 7. Let the simulation run
    pthread_join(sensor_tid, NULL); 
    
    // Cleanup
    pthread_barrier_destroy(&g_init_barrier);
    DataHub_Deinit();
    printf("========== Application Finished ==========\n");
    return 0;
}
