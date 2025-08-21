#include "myconet.hpp"
#include <stdlib.h>
#include <string.h>
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <list>
#include <vector>
#include <functional>
#include <atomic>
#include <array>

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
    using_cache(false)
{
    if (event_cb == nullptr)
        event_mask = EVENT_NONE;
    
    if (cache_size > 0 && conflags & CONF_CACHED) {
        cache.resize(cache_size);
        using_cache = true;
    }

    if (notify_size > 0 && conflags & CONF_NOTIFY_SIZE_CHECK)
        check_notify_size = true;
    
}

int MycoNode::Subscribe(std::string target_node_name)
{
    if (event_cb == nullptr || event_mask == EVENT_NONE)
        return MN_ERR_NOSUPPORT;
    
    auto target_node = net.GetNode(target_node_name);
    // add to pending list
    if (target_node.first == INVALID_ID) {
        PendingItem item = {};
        item.node_id = id;
        item.target_node_name = target_node_name;

        std::lock_guard<std::mutex> lock(net.pending_list_mutex);
        net.pending_list.push_back(item);

        return MN_INFO_PENDING;
    }
    // subscribe
    {
        // dead write-lock prevention
        std::scoped_lock lock(subscription_list_lock, target_node.second->subscriber_list_lock);
        target_node.second->subscriber_list.push_back(id);
        subscription_list.push_back(target_node.first);
    }
    // notify latched when subscribed
    std::shared_ptr<MycoNode> &node = target_node.second;
    const NodeID &node_id = target_node.first;
    if (node->using_cache && node->event_mask & EVENT_LATCHED) {
        std::shared_lock<std::shared_mutex> lock(node->cache_lock);
        EventParam param = {};
        param.event = EVENT_LATCHED;
        param.sender = node_id;
        param.recver = id;
        param.data_p = static_cast<void *>(node->cache.data());
        param.size = node->cache_size;
        event_cb(&param);
    }
    return MN_OK;
}

int MycoNode::Unsubscribe(const std::shared_ptr<MycoNode> &target_node)
{
    // dead write-lock prevention
    std::scoped_lock lock(subscription_list_lock, target_node->subscriber_list_lock);
    // Remove this node from target's subscriber list
    target_node->subscriber_list.remove(id);
    // Remove target from this node's subscription list
    subscription_list.remove(target_node->id);
    
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
        param.data_p = const_cast<void*>(buf);
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
    std::vector<std::shared_ptr<MycoNode>> subscribers;
    subscribers.reserve(subscriber_list.size());
    {
        std::shared_lock<std::shared_mutex> lock(subscriber_list_lock);
        for (const auto &sub_id : subscriber_list) {
            subscribers.push_back(net.GetNode(sub_id));
        }
    }

    for (const auto &sub_node : subscribers) 
    {
        if (sub_node && sub_node->event_mask & EVENT_PUBLISH) 
        {
            EventParam param = {};
            param.event = EVENT_PUBLISH;
            param.sender = id;
            param.recver = sub_node->id;
            param.data_p = const_cast<void*>(buf);
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
        if (nodes_map.find(node_name) != nodes_map.end()) return nullptr;

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
    for (const auto& item : items_to_process) {
        auto subscriber_node = GetNode(item.node_id);
        subscriber_node->Subscribe(node_name);
    }
    return new_node;
}

int MycoNet::RemoveNode(std::string node_name)
{
    std::unique_lock<std::shared_mutex> lock(nodes_mutex);
    
    auto it = nodes_map.find(node_name);
    if (it == nodes_map.end()) return MN_ERR_NOTFOUND;
    
    NodeID node_id = it->second;
    nodes_map.erase(it);
    
    auto node_it = nodes.find(node_id);
    if (node_it != nodes.end()) {
        nodes.erase(node_it);
    }
    
    return MN_OK;
}


int MycoNet::RemoveNode(NodeID node_id)
{
    std::unique_lock<std::shared_mutex> lock(nodes_mutex);
    
    auto it = nodes.find(node_id);
    if (it == nodes.end()) return MN_ERR_NOTFOUND;
    
    std::string node_name = it->second->node_name;
    nodes.erase(it);
    
    auto name_it = nodes_map.find(node_name);
    if (name_it != nodes_map.end()) {
        nodes_map.erase(name_it);
    }
    
    return MN_OK;
}

std::shared_ptr<MycoNet> MycoNet::GetInst(const std::string& name)
{
    std::lock_guard<std::mutex> lock(insts_mutex);
    auto it = insts.find(name);
    if (it != insts.end()) return it->second;

    auto new_net = std::make_shared<MycoNet>();
    insts[name] = new_net;
    return new_net;
}

void MycoNet::DelInst(const std::string& name)
{
    std::lock_guard<std::mutex> lock(insts_mutex);
    insts.erase(name);
}
