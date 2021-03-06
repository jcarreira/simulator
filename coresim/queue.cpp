#include <climits>
#include <iostream>
#include <stdlib.h>
#include "assert.h"

#include "queue.h"
#include "packet.h"
#include "event.h"
#include "debug.h"

#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event* ev);
extern uint32_t dead_packets;
extern uint32_t unfair_drops;
extern uint32_t total_dropped_packets;
extern DCExpParams params;

uint32_t Queue::instance_count = 0;

std::unique_ptr<std::ofstream> Queue::log_file;

//#define DEBUG

/* Queues */
Queue::Queue(uint32_t id, double rate, int location) {
    this->id = id;
    this->unique_id = Queue::instance_count++;
    this->rate = rate; // in bps

    this->busy = false;
    this->queue_proc_event = nullptr;
    this->location = location;

    if (params.ddc != 0) {
        // check this! very optimistic
        // RTT at google = 10-20us
        if (location == 0) {
            this->propagation_delay = 100e-9; /// 100ns ~20m from node to agg switch
        }
        else if (location == 1 || location == 2) {
            this->propagation_delay = 1e-6; // 1us ~200m from core to agg switch
        }
        else if (location == 3) {
            this->propagation_delay = 1e-6; // 1us (~200m from core to agg switch)
        }
        else {
            throw std::runtime_error("Wrong location");
        }
    } else {
        throw std::runtime_error("Wrong propagation delay");
        this->propagation_delay = params.propagation_delay;
    }
    this->p_arrivals = 0; this->p_departures = 0;
    this->b_arrivals = 0; this->b_departures = 0;

    this->pkt_drop = 0;
    this->spray_counter=std::rand();
    this->packet_transmitting = nullptr;
}

void Queue::set_src_dst(Node *src, Node *dst) {
    this->src = src;
    this->dst = dst;
}

void Queue::open_file() const {
    if (params.shared_queue.log_file == "") {
        throw std::runtime_error("Wrong lof file name");
    }

    std::cout << "Opening log file: "
        << params.shared_queue.log_file
        << " in switch of type: " << src->type
        << std::endl;

    log_file.reset(new std::ofstream(params.shared_queue.log_file, 
                std::ofstream::out));// | std::ofstream::app));

    if (!log_file->is_open()) {
        throw std::runtime_error("Error opening the file");
    }
}

void Queue::log_queue_utilization() const {
    if (params.shared_queue.track_queue == id &&
            params.shared_queue.track_switch == src->id &&
            src->type == SWITCH) {
        static bool opened_file = false;

        if (!opened_file) {
            opened_file = true;
            open_file();
        }
       
#ifdef DEBUG 
        std::cerr << "Writing to src->id: "
            << src->id 
            << " queue id: " << id
            << std::endl;
#endif

        if (log_file.get() == nullptr) {
            std::cerr << "Wrong handle " << log_file.get() << std::endl;
            throw std::runtime_error("");
        }
        (*log_file) << get_current_time() << " " << id << " "
            << src->id << " "
            << getBytesInQueue() << std::endl;
    } else {
#ifdef DEBUG
        std::cerr << "Not logging"
            << " src->id: " << src->id
            << " queue id: " << id
            << " src->type: " << src->type
            << std::endl;
#endif
    }
}

void Queue::enque(Packet *packet) {

    packet->last_enque_time = get_current_time();

    if (params.logging.enqueue) {
        std::cout << "Enqueing queue_id: " << id
            << " switch id: " << src->id << std::endl;
    }

    // we log here the buffer occupancy
    p_arrivals += 1;
    b_arrivals += packet->size;
    if (getBytesInQueue() + packet->size <= getQueueLimitBytes()) {
        packets.push_back(packet);
        setBytesInQueue(getBytesInQueue() + packet->size);
    } else {
        drop(packet);
    }

    log_queue_utilization();
}

Packet *Queue::deque() {
    if (getBytesInQueue() > 0) {
        Packet *p = packets.front();

        p->total_queuing_delay += get_current_time() - p->last_enque_time;

        packets.pop_front();
        setBytesInQueue(getBytesInQueue() - p->size);
        p_departures += 1;
        b_departures += p->size;
        return p;
    }
    return nullptr;
}

void Queue::drop(Packet *packet) {
    pkt_drop++;
    total_dropped_packets++;
    packet->flow->pkt_drop++;

    if(packet->seq_no < packet->flow->size){
        packet->flow->data_pkt_drop++;
    }

    if(packet->type == ACK_PACKET) {
        packet->flow->ack_pkt_drop++;
    }

    if (location != 0 && packet->type == NORMAL_PACKET) {
        dead_packets += 1;
    }

    if (debug_flow(packet->flow->id)) {
        std::cout << get_current_time() << " pkt drop. flow:" << packet->flow->id
            << " type:" << packet->type << " seq:" << packet->seq_no
            << " at queue id:" << this->id << " loc:" << this->location << "\n";
    }

    delete packet;
}

double Queue::get_transmission_delay(uint32_t size) {
    return size * 8.0 / rate;
}

void Queue::preempt_current_transmission() {
    if(params.preemptive_queue && busy){
        this->queue_proc_event->cancelled = true;
        assert(this->packet_transmitting);

        uint delete_index;
        bool found = false;
        for (delete_index = 0; delete_index < packets.size(); delete_index++) {
            if (packets[delete_index] == this->packet_transmitting) {
                found = true;
                break;
            }
        }
        if(found){
            setBytesInQueue(getBytesInQueue() - packet_transmitting->size);
            packets.erase(packets.begin() + delete_index);
        }

        for(uint i = 0; i < busy_events.size(); i++){
            busy_events[i]->cancelled = true;
        }
        busy_events.clear();
        //drop(packet_transmitting);//TODO: should be put back to queue
        enque(packet_transmitting);
        packet_transmitting = nullptr;
        queue_proc_event = nullptr;
        busy = false;
    }
}

/* Implementation for probabilistically dropping queue */
ProbDropQueue::ProbDropQueue(uint32_t id, double rate, uint32_t limit_bytes,
        double drop_prob, int location)
    : StaticQueue(id, rate, limit_bytes, location) {
        this->drop_prob = drop_prob;
}

void ProbDropQueue::enque(Packet *packet) {
    p_arrivals += 1;
    b_arrivals += packet->size;

    if (getBytesInQueue() + packet->size <= getQueueLimitBytes()) {
        double r = (1.0 * rand()) / (1.0 * RAND_MAX);
        if (r < drop_prob) {
            return;
        }
        packets.push_back(packet);
        setBytesInQueue(getBytesInQueue() + packet->size);
        if (!busy) {
            add_to_event_queue(new QueueProcessingEvent(get_current_time(), this));
            this->busy = true;
            //if(this->id == 7) std::cout << "!!!!!queue.cpp:189\n";
            this->packet_transmitting = packet;
        }
    }
}


SharedQueue::SharedQueue(uint32_t id, double rate, std::shared_ptr<SwitchBuffer> buffer, int location) :
    Queue(id, rate, location),
    alpha(params.shared_queue.alpha),
    switch_buffer(buffer) {

    std::cerr << "Creating shared queue " 
        << " id: " << id
        << " alpha: " << alpha
        << " propagation_delay: " << propagation_delay
        << " location: " << location
        << '\n';
}

void SharedQueue::check_unfair_drops() const {
    if (getBytesInQueue() <
            switch_buffer->getBufferSize() / switch_buffer->getActiveQueues()) {
        unfair_drops++;
    }
}

void SharedQueue::drop(Packet *packet) {
#ifdef DEBUG
        std::cerr << "WARNING: Dropping packet. id: " << id 
            << " label: " << src->getLabel()
            << " queue limit_bytes: " << getQueueLimitBytes()
            << " bytes_in_queue: " << getBytesInQueue()
            << " buffer size: " << switch_buffer->getBufferSize()
            << " buffer occupancy: " << switch_buffer->getBufferOccupancy()
            << " alpha: " << alpha
            << std::endl;
#endif

    check_unfair_drops();

    Queue::drop(packet);
}

void SharedQueue::enque(Packet *packet) {
#ifdef DEBUG
    std::cerr << "Enqueing packet. "
        << " queue id: " << id
        << " Buffer size: " << switch_buffer->getBufferSize()
        << " Buffer occupancy: " << switch_buffer->getBufferOccupancy()
        << " bytes in queue: " << getBytesInQueue()
        << " queue limit bytes: " << getQueueLimitBytes()
        << " queue type: " << src->type
        << std::endl;
#endif

    if (getBytesInQueue() == 0) {
        switch_buffer->incActiveQueues();
    }

    Queue::enque(packet);
}

Packet* SharedQueue::deque() {
    Packet* p = Queue::deque();

    if (p != nullptr && getBytesInQueue() == 0) {
        switch_buffer->decActiveQueues();
    }

    return p;
}

struct packet_comparator {
    bool operator()(Packet* p1, Packet* p2) const {
        if (p1->pkt_priority != p2->pkt_priority) {
            // lower prio value means higher priority packet
            return p1->pkt_priority > p2->pkt_priority;
        } else {
            // if both packets are in the same queue
            // the one that goes has been waiting longer
            return p1->last_enque_time > p2->last_enque_time;
        }

    }
};

MultiSharedQueue::MultiSharedQueue(uint32_t id, double rate, std::shared_ptr<SwitchBuffer> buffer, int location) :
    Queue(id, rate, location),
    alpha_prio(params.shared_queue.alpha_prio),
    alpha_back(params.shared_queue.alpha_back),
    switch_buffer(buffer) {

    std::cerr << "Creating multi-queue " 
        << " id: " << id
        << " alpha_prio: " << alpha_prio
        << " alpha_back: " << alpha_back
        << " propagation_delay: " << propagation_delay
        << " location: " << location
        << '\n';

    packet_queue.reset(new std::priority_queue<Packet*, std::vector<Packet*>,
                 std::function<bool(Packet*, Packet*)> >(packet_comparator()));

}

void MultiSharedQueue::enque(Packet *packet) {
   
#ifdef QUEUE_DEBUG 
    std::cerr << "Enqueue packet size: " << packet->size << std::endl;
    std::cerr << "prio: " << packet->pkt_priority << std::endl;
#endif

    packet->last_enque_time = get_current_time();

    // we log here the buffer occupancy
    p_arrivals += 1;
    b_arrivals += packet->size;

    // if we have space we enqueue it
    if (bytes_in_queue_prio + packet->size <=
            getQueueLimitBytes(packet->pkt_priority)) {
        packet_queue->push(packet);
        setBytesInQueue(getBytesInQueue() + packet->size);

        switch_buffer->setBufferOccupancy(switch_buffer->getBufferOccupancy() + packet->size);

        // if queue was empty we have to update number of active queues 
        if ( (packet->pkt_priority == HIGH_PRIO &&
                    bytes_in_queue_prio == 0) ||
                (packet->pkt_priority == LOW_PRIO &&
                 bytes_in_queue_back == 0)) {
            switch_buffer->incActiveQueues();
        }

        if (packet->pkt_priority == HIGH_PRIO) {
            bytes_in_queue_prio += packet->size;
        } else {
            bytes_in_queue_back += packet->size;
        }
    } else {
        // otherwise drop
        drop(packet);
    }
   
    assert(bytes_in_queue == bytes_in_queue_back + bytes_in_queue_prio);
    log_queue_utilization();
}

void MultiSharedQueue::check_unfair_drops(Packet* packet) const {
    // increment number of unfair drops if the number of bytes in the queue
    // is unfairly small
    if (getBytesInQueue(packet->pkt_priority) <
            switch_buffer->getBufferSize() / switch_buffer->getActiveQueues()) {
        unfair_drops++;
    }
}

void MultiSharedQueue::drop(Packet *packet) {
#ifdef QUEUE_DEBUG 
    std::cerr << "WARNING: Dropping packet. id: " << id 
        << " label: " << src->getLabel()
        << " bytes_in_queue: " << getBytesInQueue()
        << " bytes_in_queue_prio: " << bytes_in_queue_prio
        << " bytes_in_queue_back: " << bytes_in_queue_back
        << " packet prio: " << packet->pkt_priority
        << " buffer size: " << switch_buffer->getBufferSize()
        << " buffer occupancy: " << switch_buffer->getBufferOccupancy()
        << " alpha_prio: " << alpha_prio
        << " alpha_back: " << alpha_back
        << " active queues: " << switch_buffer->getActiveQueues()
        << std::endl;
#endif

    check_unfair_drops(packet);

    // count dropped pcakets and drstroy packet
    Queue::drop(packet);

    assert(bytes_in_queue == bytes_in_queue_back + bytes_in_queue_prio);
}

Packet* MultiSharedQueue::deque() {

    if (!bytes_in_queue)
        return nullptr;
        
    Packet *p = packet_queue->top();

#ifdef QUEUE_DEBUG 
    std::cerr << "dequeing queue # size: " << packet_queue->size() << std::endl;
    std::cerr << "bytes_in_queue_prio: " << bytes_in_queue_prio << std::endl;
    std::cerr << "bytes_in_queue_back: " << bytes_in_queue_back << std::endl;
    std::cerr << "packet_dequeue prio: " << p->pkt_priority << std::endl;
#endif

    if (p->pkt_priority == LOW_PRIO &&
            bytes_in_queue_prio > 0) {
        throw std::runtime_error("Error in dequeueing");
    }

    p->total_queuing_delay += get_current_time() - p->last_enque_time;

    packet_queue->pop();
    setBytesInQueue(getBytesInQueue() - p->size);
    p_departures += 1;
    b_departures += p->size;

    // update bytes count for specific priority
    if (p->pkt_priority == HIGH_PRIO) {
        if (bytes_in_queue_prio < p->size)
            throw std::runtime_error("fail");
        
        bytes_in_queue_prio -= p->size;
    } else if(p->pkt_priority == LOW_PRIO) {
        if (bytes_in_queue_back < p->size)
            throw std::runtime_error("fail");

        bytes_in_queue_back -= p->size;
    }
    
    // one queue zerod out so one less active queue
    if ( (p->pkt_priority == HIGH_PRIO &&
            bytes_in_queue_prio == 0) ||
            (p->pkt_priority == LOW_PRIO &&
             bytes_in_queue_back == 0)) {
        switch_buffer->decActiveQueues();
    }
           
    // update switch buffer occupancy 
    switch_buffer->setBufferOccupancy(switch_buffer->getBufferOccupancy() - p->size);
   
    assert(bytes_in_queue == bytes_in_queue_back + bytes_in_queue_prio);

    return p;
}

uint32_t MultiSharedQueue::getBytesInQueue(uint32_t prio) const {
    switch (prio) {
        case HIGH_PRIO:
            return bytes_in_queue_prio;
        case LOW_PRIO:
            return bytes_in_queue_back;
        default:
            throw std::runtime_error("Wrong priority");
    }
}
        
uint32_t MultiSharedQueue::getQueueLimitBytes(uint32_t prio) const { //XXX hack
    switch (prio) {
        case HIGH_PRIO:
            return alpha_prio * switch_buffer->getFreeSize();
        case LOW_PRIO:
            return alpha_back * switch_buffer->getFreeSize();
        default:
            throw std::runtime_error("Wrong priority");
    }
}
