#ifndef QUEUE_H
#define QUEUE_H

#include <deque>
#include <stdint.h>
#include <vector>
#include <memory>
#include <fstream>
#include <iostream>

class Node;
class Packet;
class Event;

class QueueProcessingEvent;
class PacketPropagationEvent;

class Queue {
    public:
        Queue(uint32_t id, double rate, int location);
        virtual ~Queue() { std::cout << "Queue dying" << std::endl;}
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

        virtual uint32_t getBytesInQueue() const = 0;
        virtual void setBytesInQueue(uint32_t bytes) = 0;
        virtual uint32_t getQueueLimitBytes() const = 0;

        virtual QueueProcessingEvent* getQueueProcEvent() { return queue_proc_event; }
        virtual QueueProcessingEvent* setQueueProcEvent(QueueProcessingEvent* qe) {
            queue_proc_event = qe;
            return queue_proc_event;
        }

        static void setInstanceCount(uint32_t count) { instance_count = count; }

        void log_queue_utilization() const;
        void open_file() const;
        
    protected:
        // Members
        uint32_t id;
        uint32_t unique_id;
        static uint32_t instance_count;
        double rate;
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
        
        // file to log buffer utilization of one queue
        static std::unique_ptr<std::ofstream> log_file;

};

class StaticQueue : public Queue {
    public:
        StaticQueue(uint32_t id, double rate, uint32_t limit_bytes, int location) :
            Queue(id, rate, location),
            bytes_in_queue(0),
            limit_bytes_(limit_bytes) {}
        virtual ~StaticQueue() = default;
        
    uint32_t getBytesInQueue() const override {
        return bytes_in_queue; 
    }

    void setBytesInQueue(uint32_t bytes) override {
        bytes_in_queue = bytes;
    }
        
    uint32_t getQueueLimitBytes() const override {
        return limit_bytes_;
    }

private:
    uint32_t bytes_in_queue;
    uint32_t limit_bytes_;

};

struct SwitchBuffer {
public:
    SwitchBuffer(uint32_t bsize, uint32_t bocc = 0) :
        buffer_size(bsize),
        buffer_occupancy(bocc) {

    }
    
    virtual uint32_t getBufferSize() const {
        return buffer_size;
    }

    virtual uint32_t getFreeSize() const {
        return buffer_size - buffer_occupancy;
    }
    
    virtual uint32_t getBufferOccupancy() const {
        return buffer_occupancy;
    }
    
    virtual void setBufferOccupancy(uint32_t bo) {
        buffer_occupancy = bo;
    }
    
    virtual void incActiveQueues() {
        if (num_active_queues > 20)
            throw std::runtime_error("too much incActiveQueues");
        num_active_queues++;
    }
    
    virtual void decActiveQueues() {
        if (num_active_queues == 0)
            throw std::runtime_error("too much decActiveQueues");
        num_active_queues--;
    }
    
    virtual uint32_t getActiveQueues() const {
        return num_active_queues;
    }

protected:
    uint32_t buffer_size; // bytes
    uint32_t buffer_occupancy = 0;
    uint32_t num_active_queues = 0;
};

class SharedQueue : public Queue {
    public:
        SharedQueue(uint32_t id, double rate, std::shared_ptr<SwitchBuffer> buffer, int location);
        virtual ~SharedQueue() = default;

        virtual uint32_t getQueueLimitBytes() const override {
            uint32_t queueLimitBytes = alpha * switch_buffer->getFreeSize();
            return queueLimitBytes;
        }
        
        void drop(Packet *packet) override;
        
        uint32_t getBytesInQueue() const override {
            return bytes_in_queue;
        }

        void setBytesInQueue(uint32_t bytes) override {
            // update the buffer occupancy
            if (bytes > bytes_in_queue) {
                switch_buffer->setBufferOccupancy(
                        switch_buffer->getBufferOccupancy() + (bytes - bytes_in_queue));
            } else {
                switch_buffer->setBufferOccupancy(
                        switch_buffer->getBufferOccupancy() - (bytes_in_queue - bytes));
            }
            bytes_in_queue = bytes;
        }
        
        virtual void enque(Packet *packet) override;
        virtual Packet* deque() override;
        
        virtual void check_unfair_drops() const;
        
    protected:
        uint32_t alpha;
        uint32_t bytes_in_queue;
        std::shared_ptr<SwitchBuffer> switch_buffer;
};


class ProbDropQueue : public StaticQueue {
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
private:
};

//XXX fix this
class MultiSharedQueue : public Queue {
    public:
        MultiSharedQueue(uint32_t id, double rate, std::shared_ptr<SwitchBuffer> buffer, int location);
        virtual ~MultiSharedQueue() = default;

        virtual uint32_t getQueueLimitBytes() const override {
            uint32_t queueLimitBytes = alpha * switch_buffer->getFreeSize();
            return queueLimitBytes;
        }
        
        void drop(Packet *packet) override;
        
        uint32_t getBytesInQueue() const override {
            return bytes_in_queue;
        }

        void setBytesInQueue(uint32_t bytes) override {
            // update the buffer occupancy
            if (bytes > bytes_in_queue) {
                switch_buffer->setBufferOccupancy(
                        switch_buffer->getBufferOccupancy() + (bytes - bytes_in_queue));
            } else {
                switch_buffer->setBufferOccupancy(
                        switch_buffer->getBufferOccupancy() - (bytes_in_queue - bytes));
            }
            bytes_in_queue = bytes;
        }
        
        virtual void enque(Packet *packet) override;
        virtual Packet* deque() override;
        
        virtual void check_unfair_drops() const;
        
    protected:
        uint32_t alpha;
        uint32_t alpha_prio;
        uint32_t alpha_back;
        uint32_t bytes_in_queue;
        std::shared_ptr<SwitchBuffer> switch_buffer;
};

#endif
