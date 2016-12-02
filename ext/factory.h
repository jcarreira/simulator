#ifndef FACTORY_H
#define FACTORY_H

#include "../coresim/flow.h"
#include "../coresim/node.h"
#include "../coresim/queue.h"

/* Queue types */

enum QueueType {
    DROPTAIL_QUEUE = 1,
    PFABRIC_QUEUE = 2,
    PROB_DROP_QUEUE = 4,
    DCTCP_QUEUE = 5,
    DROPTAIL_SHARED_QUEUE = 100
};

/* Flow types */
enum FlowType {
    NORMAL_FLOW = 1,
    PFABRIC_FLOW = 2,
    VANILLA_TCP_FLOW = 42,
    DCTCP_FLOW = 43,
    CAPABILITY_FLOW = 112,
    MAGIC_FLOW = 113,
    FASTPASS_FLOW = 114,
    IDEAL_FLOW = 120
};


/* Host types */

enum HostType {
    NORMAL_HOST = 1,
    SCHEDULING_HOST = 2,
    CAPABILITY_HOST = 12,
    MAGIC_HOST = 13,
    FASTPASS_HOST = 14,
    FASTPASS_ARBITER = 10,
    IDEAL_HOST = 20
};

class Factory {
    public:
        static int flow_counter;
        static Flow *get_flow(
                uint32_t id, 
                double start_time, 
                uint32_t size,
                Host *src, 
                Host *dst, 
                uint32_t flow_type,
                double paced_rate = 0.0
                );

        static Flow *get_flow(
                double start_time, 
                uint32_t size,
                Host *src, 
                Host *dst, 
                uint32_t flow_type,
                double paced_rate = 0.0
                );

        static Queue *get_queue(
                uint32_t id, 
                double rate,
                uint32_t queue_size, 
                uint32_t type,
                double drop_prob, 
                int location,
                std::shared_ptr<SwitchBuffer> buffer
                );

        static Host* get_host(
                uint32_t id, 
                double rate, 
                uint32_t queue_type, 
                uint32_t host_type
                );
};

#endif
