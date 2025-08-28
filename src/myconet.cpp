#include "myconet.hpp"
#include <array>
#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using namespace MycoNets;

std::map<std::string, std::shared_ptr<MycoNet>> MycoNet::insts;
std::mutex MycoNet::insts_mutex;

MycoNode::MycoNode(std::string name, const NodeParam &param, MycoNet &net) :
    node_name(name),
    id(INVALID_ID),
    conflags(param.conflags), 
    net(net),
    event_cb(param.event_cb),
    event_mask(param.event_msk),
    cache_size(param.size),
    notify_size(param.notify_size),
    user_data(param.user_data),
    check_notify_size(false),
    using_cache(false),
    trigger_latch(false)
{
    if (event_cb == nullptr)
        event_mask = EVENT_NONE;
    
    if (cache_size > 0 && conflags & CONF_CACHED) {
        cache.resize(cache_size);
        using_cache = true;
    }

    if (conflags & CONF_LATCHED && using_cache)
        trigger_latch = true;

    if (notify_size > 0 && conflags & CONF_NOTIFY_SIZE_CHECK)
        check_notify_size = true;
}

int MycoNode::Subscribe(std::string target_node_name)
{
    if (event_cb == nullptr || event_mask == EVENT_NONE)
        return MN_ERR_NOSUPPORT;

    auto [target_id, target_node] = net.GetNode(target_node_name);

    // add to pending list
    if (target_id == INVALID_ID)
    {
        PendingItem item = {};
        item.node_id = id;
        item.target_node_name = target_node_name;

        std::lock_guard<std::mutex> lock(net.pending_list_mutex);
        net.pending_list.push_back(item);

        return MN_INFO_PENDING;
    }
    // subscribe
    {
        std::unique_lock<std::shared_mutex> lock(net.spps_lock);
        net.sp_map[id].insert(target_id);
        net.ps_map[target_id].insert(id);
    }
    // notify latched when subscribed
    auto want_trigger_latch = target_node->trigger_latch;
    auto i_can_recv_latch = event_mask & EVENT_LATCHED;
    if (want_trigger_latch && i_can_recv_latch) {
        std::shared_lock<std::shared_mutex> lock(target_node->cache_lock);
        EventParam param = {};
        param.event = EVENT_LATCHED;
        param.sender = target_id;
        param.recver = id;
        param.data_p = static_cast<void *>(target_node->cache.data());
        param.size = target_node->cache_size;
        event_cb(&param);
    }
    return MN_OK;
}

int MycoNode::Unsubscribe(const std::shared_ptr<MycoNode> &target_node)
{
    std::unique_lock<std::shared_mutex> lock(net.spps_lock);
    net.sp_map[id].erase(target_node->id);
    net.ps_map[target_node->id].erase(id);
    return MN_OK;
}

int MycoNode::Pull(const std::shared_ptr<MycoNode> &target_node, void *buf, size_t size)
{
    if (!buf) return MN_ERR_NULL_POINTER;
    // Check size
    if (size != target_node->cache_size) return MN_ERR_SIZE_MISMATCH;
    
    // If target node is using cache, copy data to this node's cache and return
    if(target_node->using_cache) {
        std::shared_lock<std::shared_mutex> lock(target_node->cache_lock);
        memcpy(buf, target_node->cache.data(), size);
        return MN_INFO_CACHE_PULLED;
    }

    // Call event callback if registered for PULL events
    if (target_node->event_mask & EVENT_PULL)
    {
        EventParam param = {};
        param.event = EVENT_PULL;
        param.sender = id;
        param.recver = target_node->id;
        param.data_p = buf;
        param.size = size;
        target_node->event_cb(&param);
    }

    return MN_OK;
}

int MycoNode::Notify(const std::shared_ptr<MycoNode> &target_node, const void *buf, size_t size)
{
    if (buf == nullptr) return MN_ERR_NULL_POINTER;
    // check size
    if (target_node->check_notify_size && size != target_node->notify_size)
    {
        return MN_ERR_SIZE_MISMATCH;
    }

    // Call event callback if registered for NOTIFY events
    if (target_node->event_mask & EVENT_NOTIFY)
    {
        EventParam param = {};
        param.event = EVENT_NOTIFY;
        param.sender = id;
        param.recver = target_node->id;
        param.data_p = const_cast<void *>(buf);
        param.size = size;
        target_node->event_cb(&param);
    }

    return MN_OK;
}

int MycoNode::Unsubscribe(std::string target_node_name)
{
    auto target_node = net.GetNode(target_node_name);
    if (target_node.first == INVALID_ID) return MN_ERR_NOTFOUND;
    return Unsubscribe(target_node.second);
}

int MycoNode::Unsubscribe(NodeID target_node_id)
{
    auto target_node = net.GetNode(target_node_id);
    if (target_node == nullptr) return MN_ERR_NOTFOUND;
    return Unsubscribe(target_node);
}

int MycoNode::Publish(const void *buf, size_t size)
{
    if (buf == nullptr) return MN_ERR_NULL_POINTER;

    if (using_cache) {
        if (size != cache_size) {
            return MN_ERR_SIZE_MISMATCH;
        }
        std::unique_lock<std::shared_mutex> lock(cache_lock);
        memcpy(cache.data(), buf, size);
    }

    // copy subscribers list
    std::unique_ptr<std::set<NodeID>> subscribers = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(net.spps_lock);
        subscribers = std::make_unique<std::set<NodeID>>(net.ps_map[id]);
    }

    for (const auto &sub_id : *subscribers)
    {
        auto sub_node = net.GetNode(sub_id);
        if (sub_node && sub_node->event_mask & EVENT_PUBLISH)
        {
            EventParam param = {};
            param.event = EVENT_PUBLISH;
            param.sender = id;
            param.recver = sub_node->id;
            param.data_p = const_cast<void *>(buf);
            param.size = size;
            sub_node->event_cb(&param);
        }
    }

    return MN_OK;
}

int MycoNode::Pull(NodeID target_node_id, void *buf, size_t size)
{
    auto target_node = net.GetNode(target_node_id);
    if (target_node == nullptr) return MN_ERR_NOTFOUND;
    return Pull(target_node, buf, size);
}

int MycoNode::Pull(std::string target_node_name, void *buf, size_t size)
{
    auto target_node = net.GetNode(target_node_name);
    if (target_node.first == INVALID_ID) return MN_ERR_NOTFOUND;
    return Pull(target_node.second, buf, size);
}

int MycoNode::Notify(std::string target_node_name, const void *buf, size_t size)
{
    auto target_node = net.GetNode(target_node_name);
    if (target_node.first == INVALID_ID) return MN_ERR_NOTFOUND;
    return Notify(target_node.second, buf, size);
}

int MycoNode::Notify(NodeID target_node_id, const void *buf, size_t size)
{
    auto target_node = net.GetNode(target_node_id);
    if (target_node == nullptr) return MN_ERR_NOTFOUND;
    return Notify(target_node, buf, size);
}
int MycoNode::SubNum() {
    std::shared_lock<std::shared_mutex> lock(net.spps_lock);
    return net.ps_map[id].size();
}

int MycoNode::PubNum() {
    std::shared_lock<std::shared_mutex> lock(net.spps_lock);
    return net.sp_map[id].size();
}

// =====================================================
// =====================================================
// =====================================================
// =====================================================
// =====================================================

std::shared_ptr<MycoNode> MycoNet::NewNode(std::string node_name, const NodeParam &param)
{
    // enable std::make_shared to use private constructor
    struct MakeNewNodeEnable : public MycoNode {
        MakeNewNodeEnable(std::string node_name, const NodeParam &param, MycoNet &net) :
            MycoNode(node_name, param, net){}
    };

    std::shared_ptr<MycoNode> new_node;
    {
        std::unique_lock<std::shared_mutex> lock(nodes_mutex);
        if (nodes_map.find(node_name) != nodes_map.end())
            return nullptr;

        NodeID node_id = MakeNewNodeId();
        new_node = std::make_shared<MakeNewNodeEnable>(node_name, param, *this);
        new_node->id = node_id;
        nodes[node_id] = new_node;
        nodes_map[new_node->node_name] = node_id;
    }

    // check pending list & add to items_to_process
    std::list<PendingItem> items_to_process;
    {
        std::lock_guard<std::mutex> lock(pending_list_mutex);
        for (auto it = pending_list.begin(); it != pending_list.end();) {
            if (it->target_node_name == node_name) {
                items_to_process.push_back(std::move(*it));
                it = pending_list.erase(it);
            } else {
                ++it;
            }
        }
    }
    // process items_to_process
    for (const auto &item : items_to_process)
    {
        auto subscriber_node = GetNode(item.node_id);
        subscriber_node->Subscribe(node_name);
    }
    return new_node;
}

int MycoNet::RemoveNode(std::string node_name)
{
    NodeID node_id;
    {
        std::shared_lock<std::shared_mutex> lock(nodes_mutex);
        auto it = nodes_map.find(node_name);
        if (it == nodes_map.end()) return MN_ERR_NOTFOUND;
        node_id = it->second;
    }
    
    // 直接调用RemoveNode(NodeID)来避免重复代码并确保完整清理
    return RemoveNode(node_id);
}

int MycoNet::RemoveNode(NodeID node_id)
{
    std::shared_ptr<MycoNode> node_p = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(nodes_mutex);
        auto it = nodes.find(node_id);
        if (it == nodes.end()) return MN_ERR_NOTFOUND;
        node_p = it->second;
    }

    // step1: remove sub/pub relations
    {
        std::unique_lock<std::shared_mutex> lock(spps_lock);
        // Remove this node from all subscription maps
        ps_map.erase(node_id);
        sp_map.erase(node_id);
        
        // Remove this node_id from all other nodes' maps
        for (auto& pair : ps_map) {
            pair.second.erase(node_id);
        }
        for (auto& pair : sp_map) {
            pair.second.erase(node_id);
        }
    }

    // step2: remove node from nodes
    {
        std::unique_lock<std::shared_mutex> lock(nodes_mutex);
        node_p->id = INVALID_ID;
        // nodes_map[node_p->node_name].earse();
        nodes_map.erase(node_p->node_name);
        nodes.erase(node_id);
    }

    return MN_OK;
}

std::shared_ptr<MycoNet> MycoNet::GetInst(const std::string &name)
{
    std::lock_guard<std::mutex> lock(insts_mutex);
    auto it = insts.find(name);
    if (it != insts.end()) return it->second;

    auto new_net = std::make_shared<MycoNet>();
    insts[name] = new_net;
    return new_net;
}

void MycoNet::DelInst(const std::string &name)
{
    std::lock_guard<std::mutex> lock(insts_mutex);
    insts.erase(name);
}
