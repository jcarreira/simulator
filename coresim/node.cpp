#include "packet.h"
#include "flow.h"

#include "../ext/factory.h"

#include "../run/params.h"

#include <cassert>

extern DCExpParams params;

Node::Node(uint32_t id, uint32_t type) {
    this->id = id;
    this->type = type;
}

// TODO FIX superclass constructor
Host::Host(uint32_t id, double rate, uint32_t queue_type, uint32_t host_type) : Node(id, HOST) {
    //assert(queue_type == DROPTAIL_QUEUE); // static queue
    if (queue_type != DROPTAIL_QUEUE) {
        std::cerr << "Forcing hosts to use static queues" << std::endl;
        queue_type = DROPTAIL_QUEUE;
    }

    std::cerr << "Creating host id: " << id << std::endl;

    queue = Factory::get_queue(id, rate, params.queue_size, queue_type, 0, 0, nullptr);
    this->host_type = host_type;
}

// TODO FIX superclass constructor
Switch::Switch(uint32_t id, uint32_t switch_type) : Node(id, SWITCH) {
    this->switch_type = switch_type;
}

CoreSwitch::CoreSwitch(uint32_t id, uint32_t nq, double rate, uint32_t type) : Switch(id, CORE_SWITCH) {
    std::shared_ptr<SwitchBuffer> buffer;
    
    std::cerr << "Creating core switch id: " << id << std::endl;

    if (params.use_shared_queue == 1) {
        buffer = std::make_shared<SwitchBuffer>(params.queue_size);
    } else {
        buffer = nullptr;
    }
    
    for (uint32_t i = 0; i < nq; i++) {
        queues.push_back(Factory::get_queue(i, rate, params.queue_size, type, 0, 2, buffer));
    }
}

//nq1: # host switch, nq2: # core switch
AggSwitch::AggSwitch(
        uint32_t id, 
        uint32_t nq1, 
        double r1,
        uint32_t nq2, 
        double r2, 
        uint32_t type
        ) : Switch(id, AGG_SWITCH) {
        
    std::cerr << "Creating aggregate switch id: " << id << std::endl;

    std::shared_ptr<SwitchBuffer> buffer(
            params.use_shared_queue ? new SwitchBuffer(params.queue_size)
                                    : nullptr);

    // hosts queues
    for (uint32_t i = 0; i < nq1; i++) {
        queues.push_back(Factory::get_queue(i, r1, params.queue_size, type, 0, 3, buffer));
    }

    // core queues
    for (uint32_t i = 0; i < nq2; i++) {
        queues.push_back(Factory::get_queue(i, r2, params.queue_size, type, 0, 1, buffer));
    }
}

