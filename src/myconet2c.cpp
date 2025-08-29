#include "myconet.h"
#include "myconet.hpp"

using namespace MycoNets;

extern "C" {
MN_API int myconet_init()
{
    if (MycoNet::GetInst() == nullptr)
        return MN_ERR_NOTINITIALIZED;
    return MN_OK;
}


MN_API void myconet_deinit()
{
    MycoNet::DelInst();
}


MN_API int myconet_node_num()
{
    return MycoNet::Inst().NodeNum();
}


MN_API const char* myconet_strerr(int err)
{
    return MycoNet::StrErrCode(err);
}


MN_API int myconet_create_node(MycoNet_ID_t *id, const char *name, const MycoNet_NodeParam_t *conf)
{
    if (!id || !name || !conf) return MN_ERR_NULL_POINTER;
    
    NodeParam param = {};
    param.size = conf->size;
    param.conflags = conf->conflags;
    param.event_msk = conf->event_msk;
    param.event_cb = conf->event_cb;
    param.user_data = conf->user_data;
    param.notify_size = conf->notify_size;

    auto new_node = MycoNet::Inst().NewNode(name, param);
    if (new_node == nullptr) {
        *id = INVALID_ID;
        return MN_ERR_FAIL;
    }
    *id = new_node->MyID();
    return MN_OK;
}


MN_API int myconet_remove_node_id(MycoNet_ID_t id)
{
    return MycoNet::Inst().RemoveNode(id);
}


MN_API int myconet_remove_node_name(const char *name)
{
    NodeID id = MycoNet::Inst().NodeExists(name);
    if (id == INVALID_ID) return MN_ERR_NOTFOUND;
    return MycoNet::Inst().RemoveNode(name);
}


MN_API int myconet_subscribe(MycoNet_ID_t id, const char *target_node_name)
{
    if (target_node_name == nullptr) return MN_ERR_NULL_POINTER;
    auto node = MycoNet::Inst().GetNode(id);
    if (node == nullptr) return MN_ERR_NOTFOUND;
    return node->Subscribe(target_node_name);
}


MN_API int myconet_unsubscribe(MycoNet_ID_t id, const char *target_node_name)
{
    if (target_node_name == nullptr) return MN_ERR_NULL_POINTER;
    auto node = MycoNet::Inst().GetNode(id);
    if (node == nullptr) return MN_ERR_NOTFOUND;
    return node->Unsubscribe(target_node_name);
}


MN_API int myconet_unsubscribe_id(MycoNet_ID_t id, MycoNet_ID_t target_node_id)
{
    auto node = MycoNet::Inst().GetNode(id);
    auto target_node = MycoNet::Inst().GetNode(target_node_id);
    if (node == nullptr || target_node == nullptr) return MN_ERR_NOTFOUND;
    return node->Unsubscribe(target_node_id);
}


MN_API int myconet_publish(MycoNet_ID_t id, const void *data_p, size_t size)
{
    auto node = MycoNet::Inst().GetNode(id);
    if (node == nullptr) return MN_ERR_NOTFOUND;
    return node->Publish(data_p, size);
}


MN_API int myconet_pull(MycoNet_ID_t id, const char *target_node_name, void *data_p, size_t size)
{
    auto node = MycoNet::Inst().GetNode(id);
    if (node == nullptr) return MN_ERR_NOTFOUND;
    return node->Pull(target_node_name, data_p, size);
}

MN_API int myconet_pull_anon(const char *target_node_name, void *data_p, size_t size)
{
    return MycoNode::PullAnon(target_node_name, data_p, size);
}


MN_API int myconet_pull_id(MycoNet_ID_t id, MycoNet_ID_t target_node_id, void *data_p, size_t size)
{
    auto node = MycoNet::Inst().GetNode(id);
    if (node == nullptr) return MN_ERR_NOTFOUND;
    return node->Pull(target_node_id, data_p, size);
}


MN_API int myconet_notify(MycoNet_ID_t id, const char *target_node_name, const void *data_p, size_t size)
{
    auto node = MycoNet::Inst().GetNode(id);
    if (node == nullptr) return MN_ERR_NOTFOUND;
    return node->Notify(target_node_name, data_p, size);
}


MN_API int myconet_notify_id(MycoNet_ID_t id, MycoNet_ID_t target_node_id, const void *data_p, size_t size)
{
    auto node = MycoNet::Inst().GetNode(id);
    if (node == nullptr) return MN_ERR_NOTFOUND;
    return node->Notify(target_node_id, data_p, size);
}


MN_API int myconet_pub_num(MycoNet_ID_t id)
{
    auto node = MycoNet::Inst().GetNode(id);
    if (node == nullptr) return MN_ERR_NOTFOUND;
    return node->PubNum();
}


MN_API int myconet_sub_num(MycoNet_ID_t id)
{
    auto node = MycoNet::Inst().GetNode(id);
    if (node == nullptr) return MN_ERR_NOTFOUND;
    return node->SubNum();
}

}
