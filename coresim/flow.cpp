#include <math.h>
#include <iostream>
#include <assert.h>

#include "flow.h"
#include "packet.h"
#include "event.h"

#include "../run/params.h"

extern double get_current_time(); 
extern void add_to_event_queue(Event *);
extern int get_event_queue_size();
extern DCExpParams params;
extern uint32_t num_outstanding_packets;
extern uint32_t max_outstanding_packets;
extern uint32_t duplicated_packets_received;

Flow::Flow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d) {
#ifdef DEBUG
    std::cout << "Creating flow id: " << id << std::endl;
#endif
    this->id = id;
    this->start_time = start_time;
    this->finish_time = 0;
    this->size = size;
    this->src = s;
    this->dst = d;

    this->next_seq_no = 0;
    this->last_unacked_seq = 0;
    this->retx_event = NULL;
    this->flow_proc_event = NULL;

    this->received_bytes = 0;
    this->recv_till = 0;
    this->max_seq_no_recv = 0;
    this->cwnd_mss = params.initial_cwnd;
    this->max_cwnd = params.max_cwnd;
    this->finished = false;

    //SACK
    this->scoreboard_sack_bytes = 0;

    this->retx_timeout = params.retx_timeout_value;
    this->mss = params.mss;
    this->hdr_size = params.hdr_size;
    this->total_pkt_sent = 0;
    this->size_in_pkt = (int)ceil((double)size/mss);

    this->pkt_drop = 0;
    this->data_pkt_drop = 0;
    this->ack_pkt_drop = 0;
    this->flow_priority = 0;
    this->first_byte_send_time = -1;
    this->first_byte_receive_time = -1;
    this->first_hop_departure = 0;
    this->last_hop_departure = 0;
}

Flow::~Flow() {
}

void Flow::start_flow() {
    send_pending_data();
}

void Flow::send_pending_data() {
#ifdef DEBUG
    std::cout << "send_pending_data received_bytes: " << 
        received_bytes << " size: " << size << std::endl;
#endif
    if (last_unacked_seq < size) {
        uint32_t seq = next_seq_no;
        uint32_t window = cwnd_mss * mss + scoreboard_sack_bytes;
    
        while ((seq + mss <= last_unacked_seq + window) &&
                (seq + mss <= size)) {
            // TODO Make it explicit through the SACK list
            //if (received.count(seq) == 0) {
         //       if (id == 90981) {
         //           std::cout << "90981 send seq: " << seq << std::endl;
         //       }
                send(seq);
            next_seq_no = seq + mss;
            seq += mss;
            if (retx_event == NULL) {
                set_timeout(get_current_time() + retx_timeout);
            }
        }
    }
}

Packet *Flow::send(uint32_t seq) {
    Packet *p = NULL;

    if (first_byte_send_time == -1)
        first_byte_send_time = get_current_time();

#ifdef DEBUG
    std::cout << "send. time: " << get_current_time() << " us "
        << get_current_time()*1e6 << " s" << std::endl;
#endif

    uint32_t priority = get_priority(seq);
    p = new Packet(
            get_current_time(), 
            this, 
            seq, 
            priority, 
            mss + hdr_size, 
            src, 
            dst
            );
    this->total_pkt_sent++;

    add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, src->queue));
    return p;
}

void Flow::send_ack(uint32_t seq, std::vector<uint32_t> sack_list) {
    Packet *p = new Ack(this, seq, sack_list, hdr_size, dst, src); //Acks are dst->src

    ack_pkts_sent++;

    add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, dst->queue));
}

void Flow::receive_ack(uint32_t ack, std::vector<uint32_t> sack_list) {
    this->scoreboard_sack_bytes = sack_list.size() * mss;

    ack_pkts_received++;

    // On timeouts; next_seq_no is updated to the last_unacked_seq;
    // In such cases, the ack can be greater than next_seq_no; update it
    if (next_seq_no < ack) {
        next_seq_no = ack;
    }

    // New ack!
    if (ack > last_unacked_seq) {
        // Update the last unacked seq
        last_unacked_seq = ack;

        // Adjust cwnd
        increase_cwnd();
    
        // Send the remaining data
        send_pending_data();

        // Update the retx timer
        if (retx_event != NULL) { // Try to move
            cancel_retx_event();
            if (last_unacked_seq < size) {
                // Move the timeout to last_unacked_seq
                double timeout = get_current_time() + retx_timeout;
                set_timeout(timeout);
            }
        }

    }

    if (ack == size && !finished) {
        finished = true;
        received.clear();
        finish_time = get_current_time();
        flow_completion_time = finish_time - start_time;
        FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
        add_to_event_queue(ev);
    }
}


void Flow::receive(Packet *p) {
    if (finished) {
        delete p;
        return;
    }

    if (p->type == ACK_PACKET) {
        Ack *a = (Ack *) p;
        total_ack_queueing_time += p->total_queuing_delay;
        receive_ack(a->seq_no, a->sack_list);
    }
    else if(p->type == NORMAL_PACKET) {
        if (this->first_byte_receive_time == -1) {
            this->first_byte_receive_time = get_current_time();
        }
        this->receive_data_pkt(p);
    }
    else {
        assert(false);
    }

    delete p;
}

void Flow::receive_data_pkt(Packet* p) {
    received_count++;
    total_queuing_time += p->total_queuing_delay;

#ifdef DEBUG
    std::cout << "receive_data_pkt. time: " << get_current_time() << " us "
        << get_current_time()*1e6 << " s" << std::endl;
#endif
    if (received.count(p->seq_no) == 0) {
        received[p->seq_no] = true;
        if(num_outstanding_packets >= ((p->size - hdr_size) / (mss)))
            num_outstanding_packets -= ((p->size - hdr_size) / (mss));
        else
            num_outstanding_packets = 0;
        received_bytes += (p->size - hdr_size);
    } else {
        ::duplicated_packets_received += 1;
        this->duplicate_pkts_received += 1;
    }
    if (p->seq_no > max_seq_no_recv) {
        max_seq_no_recv = p->seq_no;
    }
    // Determing which ack to send
    uint32_t s = recv_till;
    bool in_sequence = true;
    std::vector<uint32_t> sack_list;
    while (s <= max_seq_no_recv) {
        if (received.count(s) > 0) {
            if (in_sequence) {
                recv_till += mss;
            } else {
                sack_list.push_back(s);
            }
        } else {
            in_sequence = false;
        }
        s += mss;
    }

    send_ack(recv_till, sack_list); // Cumulative Ack
}

void Flow::set_timeout(double time) {
    if (last_unacked_seq < size) {
        RetxTimeoutEvent *ev = new RetxTimeoutEvent(time, this);
        add_to_event_queue(ev);
        retx_event = ev;
    }
}


void Flow::handle_timeout() {
    next_seq_no = last_unacked_seq;
    //Reset congestion window to 1

    timeout_count++;

    cwnd_mss = 1;
    send_pending_data(); //TODO Send again
    set_timeout(get_current_time() + retx_timeout);  // TODO
}


void Flow::cancel_retx_event() {
    if (retx_event) {
        retx_event->cancelled = true;
    }
    retx_event = NULL;
}


uint32_t Flow::get_priority(uint32_t seq) {
    if (params.flow_type == 1) {
        return 1;
    }
    if(params.deadline && params.schedule_by_deadline)
    {
        return (int)(this->deadline * 1000000);
    }
    else{
        return (size - last_unacked_seq - scoreboard_sack_bytes);
    }
}


void Flow::increase_cwnd() {
    cwnd_mss += 1;
    if (cwnd_mss > max_cwnd) {
        std::cerr << "Capping congestion window size" << std::endl;
        exit(-1);
        cwnd_mss = max_cwnd;
    }
}

double Flow::get_avg_queuing_delay_in_us()
{
    return total_queuing_time/received_count * 1000000;
}

std::ostream& operator<< (std::ostream& os, const Flow& flow) {
    os << "id: " << flow.id
        << " start_time: " << flow.start_time
        << " finish_time: " << flow.finish_time
        << " size: " << flow.size
        << " src id: " << flow.src->id
        << " dst id: " << flow.dst->id
        << " next_seq_no: " << flow.next_seq_no
        << " last_unacked_seq: " << flow.last_unacked_seq
        << " received_bytes: " << flow.received_bytes
        << " total_pkt_sent: " << flow.total_pkt_sent
        << " pkt_drop: " << flow.pkt_drop
        << " data_pkt_drop: " << flow.data_pkt_drop
        << " ack_pkt_drop: " << flow.ack_pkt_drop
        << " received_count: " << flow.received_count
        << " data_pkt_drop: " << flow.data_pkt_drop
        << " flow_completion_time: " << flow.flow_completion_time
        << " total_queuing_time: " << flow.total_queuing_time
        << " first_byte_send_time: " << flow.first_byte_send_time
        << " first_byte_receive_time: " << flow.first_byte_receive_time
        << " deadline: " << flow.deadline
        << " flow_priority: " << flow.flow_priority
        << " timeout_count: " << flow.timeout_count
        << " duplicate_pkts_received: " << flow.duplicate_pkts_received
        << " ack_pkts_sent: " << flow.ack_pkts_sent
        << " ack_pkts_received: " << flow.ack_pkts_received
        << " total_ack_queueing_time: " << flow.total_ack_queueing_time
        << '\n';
    return os;
}
