#ifndef QUEUE_H
#define QUEUE_H

#include <deque>
#include <stdint.h>
#include <vector>
#include <iostream>

#define DROPTAIL_QUEUE 1
#define DROPTAIL_SHARED_QUEUE 100

class Node;
class Packet;
class Event;

class QueueProcessingEvent;
class PacketPropagationEvent;

class Queue {
    public:
        Queue(uint32_t id, double rate, uint32_t limit_bytes, int location);
        virtual ~Queue() {}
        virtual void set_src_dst(Node *src, Node *dst);
        virtual void enque(Packet *packet);
        virtual Packet *deque();
        virtual void drop(Packet *packet);
        virtual double get_transmission_delay(uint32_t size);
        virtual void preempt_current_transmission();

        virtual uint64_t getSprayCounter() const { return spray_counter; }
        virtual void incSprayCounter() { spray_counter++; }
        virtual double getPropagationDelay() const { return propagation_delay; }
        virtual uint64_t getPacketDepartures() const { return p_departures; }
        virtual uint64_t getSizeDepartures() const { return b_departures; }
        virtual uint64_t getPacketDrop() const { return pkt_drop; }
        virtual int getLocation() const { return location; }

        virtual Packet* getPacketTransmitting() const { return packet_transmitting; }
        virtual Packet* setPacketTransmitting(Packet* p) {
            packet_transmitting = p;
            return p;
        }
        
        virtual std::vector<Event*>& getBusyEvents() { return busy_events; }
        virtual Node* getDst() { return dst; }
        virtual Node* getSrc() { return src; }

        virtual double getRate() const { return rate; }
        virtual bool& getBusy() { return busy; }
        virtual bool& setBusy(bool b) { return busy = b; }

        virtual uint32_t getBytesInQueue() const { return bytes_in_queue; }
        virtual void setBytesInQueue(int bytes) {
            bytes_in_queue = bytes;
        }
        virtual uint32_t getLimitBytes() const { return limit_bytes; }

        virtual QueueProcessingEvent* getQueueProcEvent() { return queue_proc_event; }
        virtual QueueProcessingEvent* setQueueProcEvent(QueueProcessingEvent* qe) {
            queue_proc_event = qe;
            return queue_proc_event;
        }

        static void setInstanceCount(uint32_t count) { instance_count = count; }
        
    protected:
        // Members
        uint32_t id;
        uint32_t unique_id;
        static uint32_t instance_count;
        double rate;
        uint32_t limit_bytes;
        std::deque<Packet *> packets;
        bool busy;
        QueueProcessingEvent *queue_proc_event;

        std::vector<Event*> busy_events;
        Packet* packet_transmitting;

        Node *src;
        Node *dst;

        uint64_t b_arrivals, b_departures;
        uint64_t p_arrivals, p_departures;

        double propagation_delay;
        bool interested;

        uint64_t pkt_drop;
        uint64_t spray_counter;

        int location;

    private:
        uint32_t bytes_in_queue;
};

class StaticQueue : public Queue {
    public:
        StaticQueue(uint32_t id, double rate, uint32_t limit_bytes, int location) :
            Queue(id, rate, limit_bytes, location)
    {}
        ~StaticQueue(){};

};

class SharedQueue : public Queue {
    public:
        SharedQueue(uint32_t id, double rate, uint32_t limit_bytes, int location);
        virtual ~SharedQueue() {}
        virtual void set_src_dst(Node *src, Node *dst);

        uint32_t getLimitBytes() const {
            if (!limit_bytes_ptr) {
                std::cerr << "Error" << std::endl;
                exit(-1);
            }
            return *limit_bytes_ptr;
        }
        
        static uint32_t limit_bytes_array_host[200];
        static uint32_t limit_bytes_array_agg_switch[200];
        static uint32_t limit_bytes_array_core_switch[200];

    protected:
        uint32_t *limit_bytes_ptr;
        uint32_t src_type;
};


class ProbDropQueue : public Queue {
    public:
        ProbDropQueue(
                uint32_t id, 
                double rate, 
                uint32_t limit_bytes,
                double drop_prob, 
                int location
                );
        virtual void enque(Packet *packet);

        double drop_prob;
};

#endif
