#include <climits>
#include <iostream>
#include <stdlib.h>
#include "assert.h"

#include "queue.h"
#include "packet.h"
#include "event.h"
#include "debug.h"

#include "../run/params.h"

extern double get_current_time(); // TODOm
extern void add_to_event_queue(Event* ev);
extern uint32_t dead_packets;
extern DCExpParams params;

uint32_t Queue::instance_count = 0;

/* Queues */
Queue::Queue(uint32_t id, double rate, int location) {
    this->id = id;
    this->unique_id = Queue::instance_count++;
    this->rate = rate; // in bps

    this->busy = false;
    this->queue_proc_event = NULL;
    //this->packet_propagation_event = NULL;
    this->location = location;

    if (params.ddc != 0) {
        if (location == 0) {
            this->propagation_delay = 10e-9;
        }
        else if (location == 1 || location == 2) {
            this->propagation_delay = 400e-9;
        }
        else if (location == 3) {
            this->propagation_delay = 210e-9;
        }
        else {
            assert(false);
        }
    } else {
        this->propagation_delay = params.propagation_delay;
    }
    this->p_arrivals = 0; this->p_departures = 0;
    this->b_arrivals = 0; this->b_departures = 0;

    this->pkt_drop = 0;
    this->spray_counter=std::rand();
    this->packet_transmitting = NULL;

}

void Queue::set_src_dst(Node *src, Node *dst) {
    this->src = src;
    this->dst = dst;
}


void Queue::enque(Packet *packet) {
//#ifdef DEBUG
//    std::cout << "Queue enque. id: " << id
//        << " srcId: " << packet->src->id
//       << " destId: " << packet->dst->id << std::endl; 
//    std::cout << "This node label " << src->getLabel() << std::endl;
//#endif

    // we log here the buffer occupancy
    p_arrivals += 1;
    b_arrivals += packet->size;
    if (getBytesInQueue() + packet->size <= getQueueLimitBytes()) {
        packets.push_back(packet);
        setBytesInQueue(getBytesInQueue() + packet->size);
    } else {
        pkt_drop++;
        drop(packet);
    }
}

Packet *Queue::deque() {
    if (getBytesInQueue() > 0) {
        Packet *p = packets.front();
        packets.pop_front();
        setBytesInQueue(getBytesInQueue() - p->size);
        p_departures += 1;
        b_departures += p->size;
        return p;
    }
    return NULL;
}

void Queue::drop(Packet *packet) {
    packet->flow->pkt_drop++;
    if(packet->seq_no < packet->flow->size){
        packet->flow->data_pkt_drop++;
    }
    if(packet->type == ACK_PACKET) {
        //std::cout << "Dropping ack pkt" << std::endl;
        //std::cout << *(Ack*)packet << std::endl;
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
        packet_transmitting = NULL;
        queue_proc_event = NULL;
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
    alpha(params.queue_alpha),
    switch_buffer(buffer) {

    std::cerr << "Creating shared queue. id: " << id << std::endl;
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

    Queue::drop(packet);
}

void SharedQueue::set_src_dst(Node *src, Node *dst) {
    this->src = src;
    this->dst = dst;

    [[maybe_unused]]
    uint32_t src_id = src->id;
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

    Queue::enque(packet);
}

