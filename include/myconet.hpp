#ifndef MYCONET_CPP_H
#define MYCONET_CPP_H

#include "myconet.h"
#include <string>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <list>
#include <vector>
#include <functional>
#include <atomic>

namespace MycoNets { 

    // type alias
    using NodeFlag = MycoNet_NodeFlag_t;
    using EventCode = MycoNet_EventCode_t;
    using EventMask = MycoNet_EventMask_t;
    using EventParam = MycoNet_EventParam_t;
    using SmallEventParam = MycoNet_SmallEventParam_t;
    using EventCb = MycoNet_EventCb_t;
    using EventCbFn = std::function<void (const EventParam*)>;
    // TODO: small event for publish signal
    // using SmallEventCb = MycoNet_SmallEventCb_t;
    // using SmallEventCbFn = std::function<void (const SmallEventParam*)>;
    // using NodeParam = MycoNet_NodeParam_t;
    using NodeID = MycoNet_ID_t;

    struct NodeParam {
        uint32_t size;
        NodeFlag conflags;
        EventMask event_msk;
        EventCbFn event_cb;
        void *user_data;
        uint32_t notify_size;
    };

    // forward declaration
    class MycoNode;  
    class MycoNet;

    const NodeID INVALID_ID = -1;

    class MycoNode
    {
    public:
        friend class MycoNet;
        std::string node_name;
    private:
        NodeID id;
        NodeFlag conflags;
        MycoNet &net;
        EventCbFn event_cb;
        EventMask event_mask;
        std::vector<uint8_t> cache;
        mutable std::shared_mutex cache_lock;
        size_t cache_size;
        size_t notify_size;
        void *user_data;

        bool check_notify_size;
        bool using_cache;
        bool trigger_latch;
        
    public:
        MycoNode() = delete;
        ~MycoNode() = default;
        inline NodeID MyID() {return id;}
        int Subscribe(std::string target_node_name);
        int Unsubscribe(std::string target_node_name);
        int Unsubscribe(NodeID target_node_id);
        int Publish(const void *buf, size_t size);
        // TODO: features for future
        // int Publish0(std::function<void (void * const cache_p, size_t cache_size)>); // only cache enabled
        // int PublishSignal(const void *buf, size_t size) = delete;
        int Pull(NodeID target_node_id, void *buf, size_t size);
        int Pull(std::string target_node_name, void *buf, size_t size);
        static int PullAnon(std::string target_node_name, void *buf, size_t size);
        int Notify(std::string target_node_name, const void *buf, size_t size);
        int Notify(NodeID target_node_id, const void *buf, size_t size);
        // TODO: features for future
        // int Pull0(std::string target_node_name, std::function<void (const void *data_p, uint32_t size)>, size_t size); 
        // int Pull0(NodeID target_node_id, std::function<void (const void *data_p, uint32_t size)>, size_t size);
        // int Push(NodeID target_node_id, const void *buf, size_t size) = delete;
        // int Push(std::string target_node_name, const void *buf, size_t size) = delete;
        int SubNum();
        int PubNum();

    protected:
        MycoNode(std::string name, const NodeParam &param, MycoNet &net);

    private:
        int Unsubscribe(const std::shared_ptr<MycoNode> &target_node);
        int Pull(const std::shared_ptr<MycoNode> &target_node, void *buf, size_t size);
        // int Pull0(const std::shared_ptr<MycoNode> &target_node, std::function<void (const void *data_p, uint32_t size)>, size_t size);
        int Push(const std::shared_ptr<MycoNode> &target_node, const void *buf, size_t size) = delete;
        int Notify(const std::shared_ptr<MycoNode> &target_node, const void *buf, size_t size);

    };

    struct PendingItem {
        NodeID node_id;
        std::string target_node_name;
    };
    

    class MycoNet
    {
    public: 
        friend class MycoNode;
    private:
        std::map<NodeID, std::shared_ptr<MycoNode>> nodes;
        std::map<std::string, NodeID> nodes_map;
        std::shared_mutex nodes_mutex;

        std::list<PendingItem> pending_list;
        std::mutex pending_list_mutex;

        std::atomic<NodeID> next_id;
        
        // we think this is not a high-frequency operation
        std::map<NodeID, std::set<NodeID>> sp_map; // subscriber -> publisher(s)
        std::map<NodeID, std::set<NodeID>> ps_map; // publisher -> subscriber(s)
        std::shared_mutex spps_lock;

        static std::map<std::string, std::shared_ptr<MycoNet>> insts;
        static std::mutex insts_mutex;

    public:
        MycoNet() : next_id(1){};
        ~MycoNet() = default;
        MycoNet(const MycoNet&) = delete;
        MycoNet& operator=(const MycoNet&) = delete;

        std::pair<NodeID, std::shared_ptr<MycoNode>> GetNode(std::string node_name) {
            std::shared_lock<std::shared_mutex> lock(nodes_mutex);
            auto it = nodes_map.find(node_name);
            std::pair<NodeID, std::shared_ptr<MycoNode>> pair = {INVALID_ID, nullptr};
            if (it != nodes_map.end()) {
                NodeID node_id = it->second;
                auto node_it = nodes.find(node_id);
                if (node_it != nodes.end() && node_it->second->id != INVALID_ID) {
                    pair.first = node_id;
                    pair.second = node_it->second;
                }
            }
            return pair;
        }
        std::shared_ptr<MycoNode> GetNode(int node_id) {
            std::shared_lock<std::shared_mutex> lock(nodes_mutex);
            auto it = nodes.find(node_id);
            return (it != nodes.end() && it->second->id != INVALID_ID) ? it->second : nullptr;
        }

        static std::shared_ptr<MycoNet> GetInst(const std::string& name = "default");
        static void DelInst(const std::string& name = "default");
        static MycoNet& Inst() {
            return *GetInst("default");
        }
        static inline MycoNet& Self() {
            return Inst();
        }

        std::shared_ptr<MycoNode> NewNode(std::string node_name, const NodeParam &param);
        inline int NodeNum() {
            std::shared_lock<std::shared_mutex> lock(nodes_mutex);
            return nodes.size();
        }

        static const char *StrErrCode(int errnum) 
        {
            switch (errnum) {
                case MN_OK: return "Success";
                case MN_INFO_PENDING: return "Pending";
                case MN_INFO_CACHE_PULLED: return "Pulled from cache";
                case MN_ERR_FAIL: return "General failure";
                case MN_ERR_TIMEOUT: return "Timeout";
                case MN_ERR_NOMEM: return "No memory";
                case MN_ERR_NOTFOUND: return "Not found";
                case MN_ERR_NOSUPPORT: return "Not supported";
                case MN_ERR_BUSY: return "Busy";
                case MN_ERR_INVALID: return "Invalid argument";
                case MN_ERR_ACCESS: return "Access denied";
                case MN_ERR_EXIST: return "Already exists";
                case MN_ERR_NODATA: return "No data available";
                case MN_ERR_INITIALIZED: return "Already initialized";
                case MN_ERR_NOTINITIALIZED: return "Not initialized";
                case MN_ERR_SIZE_MISMATCH: return "Size mismatch";
                case MN_ERR_NULL_POINTER: return "Null pointer";
                default: return "Unknown code";
            }
        }

        NodeID MakeNewNodeId() {return next_id.fetch_add(1);}
        int RemoveNode(std::string node_name);
        int RemoveNode(NodeID node_id);

        NodeID NodeExists(std::string node_name) {
            std::shared_lock<std::shared_mutex> lock(nodes_mutex);
            auto it = nodes_map.find(node_name);
            if (it != nodes_map.end()) return it->second;
            return INVALID_ID; // not found
        }

        bool NodeExists(int node_id) {
            std::shared_lock<std::shared_mutex> lock(nodes_mutex);
            return nodes.count(node_id) ? true : false;
        }

    };

}


#endif // MYCONET_CPP_H