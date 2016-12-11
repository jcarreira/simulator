//
//  event.cpp
//  TurboCpp
//
//  Created by Gautam Kumar on 3/9/14.
//
//

#include <iomanip>

#include "run/stats.h"
#include "event.h"
#include "packet.h"
#include "topology.h"
#include "debug.h"
#include <mutex>

#include "../ext/factory.h"

#include "../run/params.h"

extern Topology* topology;
extern std::priority_queue<Event*, std::vector<Event*>, EventComparator> event_queue;
extern double current_time;
extern DCExpParams params;
extern std::deque<Event*> flow_arrivals;
extern std::deque<Flow*> flows_to_schedule;

extern uint32_t num_outstanding_packets;
extern uint32_t max_outstanding_packets;
extern uint32_t unfair_drops;

extern uint32_t num_outstanding_packets_at_50;
extern uint32_t num_outstanding_packets_at_100;
extern uint32_t arrival_packets_at_50;
extern uint32_t arrival_packets_at_100;
extern uint32_t arrival_packets_count;
extern uint32_t total_finished_flows;

extern uint32_t backlog3;
extern uint32_t backlog4;
extern uint32_t duplicated_packets_received;
extern uint32_t duplicated_packets;
extern uint32_t injected_packets;
extern uint32_t completed_packets;
extern uint32_t total_completed_packets;
extern uint32_t dead_packets;
extern uint32_t sent_packets;

extern EmpiricalRandomVariable *nv_bytes;

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern int get_event_queue_size();

uint32_t Event::instance_count = 0;

std::ofstream fct_fout;

Event::Event(uint32_t type, double time) {
    this->type = type;
    this->time = time;
    this->cancelled = false;
    this->unique_id = Event::instance_count++;
}

Event::~Event() {
}


/* Flow Arrival */
FlowCreationForInitializationEvent::FlowCreationForInitializationEvent(
        double time, 
        Host *src, 
        Host *dst,
        EmpiricalRandomVariable *nv_bytes, 
        RandomVariable *nv_intarr
    ) : Event(FLOW_CREATION_EVENT, time) {
    this->src = src;
    this->dst = dst;
    this->nv_bytes = nv_bytes;
    this->nv_intarr = nv_intarr;
}

FlowCreationForInitializationEvent::~FlowCreationForInitializationEvent() {}

void FlowCreationForInitializationEvent::process_event() {
    uint32_t id = flows_to_schedule.size();

    // truncate(val + 0.5) equivalent to round to nearest int
    uint64_t nvVal = (nv_bytes->value() + 0.5); // number of packets

    if (nvVal > 2500000) {
        std::cout << "Giant Flow! event.cpp::FlowCreation:"
            << 1000000.0 * time
            << " Generating new flow " << id
            << " of size " << (nvVal*1460)
            << " between " << src->id << " " << dst->id << "\n";
        nvVal = 2500000;
    }

    uint32_t size = (uint32_t) nvVal * 1460;
    
    if (size != 0) {
        auto new_flow = Factory::get_flow(id, time, size, src, dst, params.flow_type);
    
        // schedule flow
        flows_to_schedule.push_back(new_flow);
    }

    double tnext = time + nv_intarr->value();

    add_to_event_queue(
            new FlowCreationForInitializationEvent(
                tnext,
                src, 
                dst,
                nv_bytes, 
                nv_intarr
                )
            );
}


/* Flow Arrival */
uint32_t flow_arrival_count = 0;

FlowArrivalEvent::FlowArrivalEvent(double time, Flow* flow) : Event(FLOW_ARRIVAL, time) {
    this->flow = flow;
}

FlowArrivalEvent::~FlowArrivalEvent() {
}

void FlowArrivalEvent::process_event() {
    //Flows start at line rate; so schedule a packet to be transmitted
    //First packet scheduled to be queued

    num_outstanding_packets += (this->flow->size / this->flow->mss);
    arrival_packets_count += this->flow->size_in_pkt;
    if (num_outstanding_packets > max_outstanding_packets) {
        max_outstanding_packets = num_outstanding_packets;
    }
    this->flow->start_flow();
    flow_arrival_count++;
    if (flow_arrivals.size() > 0) {
        add_to_event_queue(flow_arrivals.front());
        flow_arrivals.pop_front();
    }

    if(params.num_flows_to_run > 10 && flow_arrival_count % 100000 == 0){
        double curr_time = get_current_time();
        uint32_t num_unfinished_flows = 0;
        for (uint32_t i = 0; i < flows_to_schedule.size(); i++) {
            Flow *f = flows_to_schedule[i];
            if (f->start_time < curr_time) {
                if (!f->finished) {
                    num_unfinished_flows ++;
                }
            }
        }
        if(flow_arrival_count == (params.num_flows_to_run * 0.5))
        {
            arrival_packets_at_50 = arrival_packets_count;
            num_outstanding_packets_at_50 = num_outstanding_packets;
        }
        if(flow_arrival_count == params.num_flows_to_run)
        {
            arrival_packets_at_100 = arrival_packets_count;
            num_outstanding_packets_at_100 = num_outstanding_packets;
        }
        std::cout << "## " << current_time << " NumPacketOutstanding " << num_outstanding_packets
            << " NumUnfinishedFlows " << num_unfinished_flows << " StartedFlows " << flow_arrival_count
            << " StartedPkts " << arrival_packets_count << "\n";
    }
}


/* Packet Queuing */
PacketQueuingEvent::PacketQueuingEvent(double time, Packet *packet,
        Queue *queue) : Event(PACKET_QUEUING, time) {
    this->packet = packet;
    this->queue = queue;
}

PacketQueuingEvent::~PacketQueuingEvent() {
}

void PacketQueuingEvent::process_event() {
    if (!queue->getBusy()) {
        queue->setQueueProcEvent(new QueueProcessingEvent(get_current_time(), queue));
        add_to_event_queue(queue->getQueueProcEvent());
        queue->getBusy() = true;
        queue->setPacketTransmitting(packet);
    } else if (params.preemptive_queue &&
            this->packet->pf_priority < queue->getPacketTransmitting()->pf_priority) {

        double remaining_percentage =
            (queue->getQueueProcEvent()->time - get_current_time()) / 
            queue->get_transmission_delay(queue->getPacketTransmitting()->size);

        if(remaining_percentage > 0.01){
            queue->preempt_current_transmission();

            queue->setQueueProcEvent(new QueueProcessingEvent(get_current_time(), queue));
            add_to_event_queue(queue->getQueueProcEvent());
            queue->getBusy() = true;
            queue->setPacketTransmitting(packet);
        }
    }

    queue->enque(packet);
}

/* Packet Arrival */
PacketArrivalEvent::PacketArrivalEvent(double time, Packet *packet)
    : Event(PACKET_ARRIVAL, time) {
        this->packet = packet;
    }

PacketArrivalEvent::~PacketArrivalEvent() {
}

void PacketArrivalEvent::process_event() {
    if (packet->type == NORMAL_PACKET) {
        completed_packets++;
    }

    packet->flow->receive(packet);
}


/* Queue Processing */
QueueProcessingEvent::QueueProcessingEvent(double time, Queue *queue)
    : Event(QUEUE_PROCESSING, time) {
        this->queue = queue;
}

QueueProcessingEvent::~QueueProcessingEvent() {
    if (queue->getQueueProcEvent() == this) {
        queue->setQueueProcEvent(nullptr);
        queue->getBusy() = false; //TODO is this ok??
    }
}

void QueueProcessingEvent::process_event() {
    Packet *packet = queue->deque();
    if (packet) {
        queue->setBusy(true);
        queue->getBusyEvents().clear();
        queue->setPacketTransmitting(packet);
        Queue *next_hop = topology->get_next_hop(packet, queue);
        double td = queue->get_transmission_delay(packet->size);
        double pd = queue->getPropagationDelay();
        //double additional_delay = 1e-10;
#ifdef DEBUG
            std::cout << "Creating QueueProcessingEvent. time: " << time
                << " td: " << td << std::endl;
#endif
        queue->setQueueProcEvent(new QueueProcessingEvent(time + td, queue));
        add_to_event_queue(queue->getQueueProcEvent());
        queue->getBusyEvents().push_back(queue->getQueueProcEvent());
        if (next_hop == nullptr) {
#ifdef DEBUG
            std::cout << "Creating PacketArrivalEvent. time: " << time
                << " td: " << td
                << " pd: " << pd << std::endl;
#endif
            Event* arrival_evt = new PacketArrivalEvent(time + td + pd + params.nw_stack_delay,
                    packet);
            add_to_event_queue(arrival_evt);
            queue->getBusyEvents().push_back(arrival_evt);
        } else {
            Event* queuing_evt = nullptr;
            if (params.cut_through == 1) {
                double cut_through_delay =
                    queue->get_transmission_delay(packet->flow->hdr_size);
                queuing_evt = new PacketQueuingEvent(time + cut_through_delay + pd, packet, next_hop);
            } else {
#ifdef DEBUG
            std::cout << "Creating PacketQueuingEvent. time: " << time
                << " td: " << td
                << " pd: " << pd << std::endl;
#endif
                queuing_evt = new PacketQueuingEvent(time + td + pd, packet, next_hop);
            }

            add_to_event_queue(queuing_evt);
            queue->getBusyEvents().push_back(queuing_evt);
        }
    } else {
        queue->setBusy(false);
        queue->getBusyEvents().clear();
        queue->setPacketTransmitting(nullptr);
        queue->setQueueProcEvent(nullptr);
    }
}


LoggingEvent::LoggingEvent(double time) : Event(LOGGING, time){
    this->ttl = 1e10;
}

LoggingEvent::LoggingEvent(double time, double ttl) : Event(LOGGING, time){
    this->ttl = ttl;
}

LoggingEvent::~LoggingEvent() {
}

void LoggingEvent::process_event() {
    double current_time = get_current_time();
    bool finished_simulation = true;
    uint32_t second_num_outstanding = 0;
    uint32_t num_unfinished_flows = 0;
    uint32_t started_flows = 0;
    for (uint32_t i = 0; i < flows_to_schedule.size(); i++) {
        Flow *f = flows_to_schedule[i];
        if (finished_simulation && !f->finished) {
            std::cout << "Flow id: " << f->id << " Not finished" << std::endl;
            std::cout << (*f) << std::endl;
            finished_simulation = false;
        }
        if (f->start_time < current_time) {
            second_num_outstanding += (f->size - f->received_bytes);
            started_flows ++;
            if (!f->finished) {
                num_unfinished_flows ++;
            }
        }
    }

    uint32_t theoretical_injection_rate = params.bandwidth * params.num_hosts * params.load * 0.01 / ((params.mss + params.hdr_size) * 8);
   
    uint32_t totalSentFromHosts = 0;
    for (auto& h : topology->hosts) {
        totalSentFromHosts += h->queue->getPacketDepartures();
    }
    uint32_t sentInTimeslot = (totalSentFromHosts - sent_packets) / 2;
    uint32_t injectedInTimeslot = arrival_packets_count - injected_packets;
    uint32_t duplicatedInTimeslot = duplicated_packets_received - duplicated_packets;
    backlog3 += (injectedInTimeslot - (completed_packets - duplicatedInTimeslot));
    backlog4 += (theoretical_injection_rate - (completed_packets - duplicatedInTimeslot));

    total_completed_packets += completed_packets;
    sent_packets = totalSentFromHosts;
    injected_packets = arrival_packets_count;
    duplicated_packets = duplicated_packets_received;

    // calculate distribution of switch buffer occupancies
    Stats buffer_occupancy;
    Stats agg_buffer_occupancy;
    Stats core_buffer_occupancy;
    for (auto& s : topology->switches) {
        buffer_occupancy += s->getBufferOccupancy();
    }

    PFabricTopology* pTopology = dynamic_cast<PFabricTopology*>(topology);
    for (auto& s : pTopology->agg_switches) {
        agg_buffer_occupancy += s->getBufferOccupancy();
    }
    for (auto& s : pTopology->core_switches) {
        core_buffer_occupancy += s->getBufferOccupancy();
    }

    std::cout << "LoggingEvent " << '\n'
        << "current time: " << current_time * 1e6 
        << '\n'
        << "dead pkts: " << dead_packets << '\t'
        << "completed pkts: " << completed_packets  << '\t'
        << "sent pkts: " << sentInTimeslot << '\t'
        << "total received pkts: " << total_completed_packets  << '\t'
        << "total duplicated pkts: " << duplicated_packets_received << '\t'
        << "total useful pkts: " << total_completed_packets - duplicated_packets_received << '\t'
        << "total injected pkts: " << arrival_packets_count 
        << '\n'
        << "backlog1: " << num_outstanding_packets << '\t'
        << "est backlog: " << backlog4
        << '\n'
        << "src util: " << ((double) sentInTimeslot) / theoretical_injection_rate << '\t'
        << "goodput: " << ((double) completed_packets - duplicatedInTimeslot) / theoretical_injection_rate
        << '\n'
        << "buffer occupancy (average): " << buffer_occupancy.avg()  << '\t'
        << "buffer occupancy (99%): " << buffer_occupancy.get_percentile(0.99)
        << '\n'
        << "agg buffer occupancy (average): " << agg_buffer_occupancy.avg()  << '\t'
        << "agg buffer occupancy (99%): " << agg_buffer_occupancy.get_percentile(0.99)
        << '\n'
        << "core buffer occupancy (average): " << core_buffer_occupancy.avg()  << '\t'
        << "core buffer occupancy (99%): " << core_buffer_occupancy.get_percentile(0.99)
        << '\n'
        << "unfair drops: " << unfair_drops
        << std::endl;
    
    dead_packets = 0;
    completed_packets = 0;
    
    if (!finished_simulation && ttl > get_current_time()) {
        add_to_event_queue(new LoggingEvent(current_time + 0.01, ttl));
    }

    /*
    std::cout << current_time
        << " MaxPacketOutstanding " << max_outstanding_packets
        << " NumPacketOutstanding " << num_outstanding_packets
        << " NumUnfinishedFlows " << num_unfinished_flows
        << " StartedFlows " << started_flows << "\n";

    if (!finished_simulation && ttl > get_current_time()) {
        add_to_event_queue(new LoggingEvent(current_time + 10, ttl));
    }
    */


}


/* Flow Finished */
FlowFinishedEvent::FlowFinishedEvent(double time, Flow *flow)
    : Event(FLOW_FINISHED, time) {
        this->flow = flow;
    }

FlowFinishedEvent::~FlowFinishedEvent() {}

void open_fct_log(const std::string& filename) {
    if (filename == "")
        throw std::runtime_error("Wrong filename: " + filename);

    std::cout << "Opening fliename: " << filename << std::endl;

    fct_fout.open(filename, std::fstream::out);
}

void FlowFinishedEvent::process_event() {
    this->flow->finished = true;
    this->flow->finish_time = get_current_time();
    this->flow->flow_completion_time = this->flow->finish_time - this->flow->start_time;
    total_finished_flows++;
#if 0
    //auto slowdown = 1000000 * flow->flow_completion_time / topology->get_oracle_fct(flow);
    if (slowdown < 1.0 && slowdown > 0.9999) {
        slowdown = 1.0;
    }
    assert(slowdown >= 1.0);
#endif

    if (print_flow_result()) {


        std::stringstream ss;

        ss << std::setprecision(9) << std::fixed
            << flow->id << " "
            << flow->size << " "
            << flow->src->id << " "
            << flow->dst->id << " "
            << 1000000 * flow->start_time << " "
            << 1000000 * flow->finish_time << " "
            << 1000000.0 * flow->flow_completion_time << " "
            << topology->get_oracle_fct(flow) << " "
            //<< slowdown << " "
            << flow->total_pkt_sent << "/" << (flow->size/flow->mss) << "//" << flow->received_count << " "
            << flow->data_pkt_drop << "/" << flow->ack_pkt_drop << "/" << flow->pkt_drop << " "
            << 1000000 * (flow->first_byte_send_time - flow->start_time) << " "
            << '\n';

        if (params.logging.fct == 1) {
            params.logging.fct = 2;
            open_fct_log(params.fct_filename);
        }

        if (params.logging.fct) {
            fct_fout << ss.str();
        }

        std::cout << std::setprecision(4) << std::fixed
            << ss.str();

        if (flow->id % 10000 == 0) {
            std::cout << (*flow) << std::endl;
        }
    }
}


/* Flow Processing */
FlowProcessingEvent::FlowProcessingEvent(double time, Flow *flow)
    : Event(FLOW_PROCESSING, time) {
        this->flow = flow;
    }

FlowProcessingEvent::~FlowProcessingEvent() {
    if (flow->flow_proc_event == this) {
        flow->flow_proc_event = nullptr;
    }
}

void FlowProcessingEvent::process_event() {
    this->flow->send_pending_data();
}


/* Retx Timeout */
RetxTimeoutEvent::RetxTimeoutEvent(double time, Flow *flow)
    : Event(RETX_TIMEOUT, time) {
        this->flow = flow;
    }

RetxTimeoutEvent::~RetxTimeoutEvent() {
    if (flow->retx_event == this) {
        flow->retx_event = nullptr;
    }
}

void RetxTimeoutEvent::process_event() {
    flow->handle_timeout();
}

