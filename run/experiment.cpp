#include <iostream>
#include <algorithm>
#include <fstream>
#include <stdlib.h>
#include <deque>
#include <stdint.h>
#include <cstdlib>
#include <ctime>
#include <map>
#include <iomanip>
#include "assert.h"
#include "math.h"

#include "../coresim/flow.h"
#include "../coresim/packet.h"
#include "../coresim/node.h"
#include "../coresim/event.h"
#include "../coresim/topology.h"
#include "../coresim/queue.h"
#include "../coresim/random_variable.h"

#include "../ext/factory.h"
#include "../ext/fountainflow.h"
#include "../ext/capabilityflow.h"
#include "../ext/fastpassTopology.h"

#include "flow_generator.h"
#include "stats.h"
#include "params.h"

#include "../ext/ideal.h"

extern Topology *topology;
extern double current_time;
extern std::priority_queue<Event*, std::vector<Event*>, EventComparator> event_queue;
extern std::deque<Flow*> flows_to_schedule;
extern std::deque<Event*> flow_arrivals;

extern uint32_t num_outstanding_packets;
extern uint32_t max_outstanding_packets;
extern DCExpParams params;
extern void add_to_event_queue(Event*);
extern void read_experiment_parameters(std::string conf_filename, uint32_t exp_type);
extern void read_flows_to_schedule(std::string filename, uint32_t num_lines, Topology *topo);
extern uint32_t duplicated_packets_received;

extern uint32_t num_outstanding_packets_at_50;
extern uint32_t num_outstanding_packets_at_100;
extern uint32_t arrival_packets_at_50;
extern uint32_t arrival_packets_at_100;

extern double start_time;
extern double get_current_time();

extern void run_scenario();

void validate_flow(Flow* f){
    double slowdown = 1000000.0 * f->flow_completion_time / topology->get_oracle_fct(f);
    if(slowdown < 0.999999){
        std::cout << "Flow " << f->id << " has slowdown " << slowdown << "\n";
        //assert(false);
    }
    //if(f->first_byte_send_time < 0 || f->first_byte_send_time < f->start_time - INFINITESIMAL_TIME)
    //    std::cout << "Flow " << f->id << " first_byte_send_time: " << f->first_byte_send_time << " start time:"
    //        << f->start_time << "\n";
}

void debug_flow_stats(std::deque<Flow *> flows){
    std::map<int,int> freq;
    for (uint32_t i = 0; i < flows.size(); i++) {
        Flow *f = flows[i];
        if(f->size_in_pkt == 3){
            int fct = (int)(1000000.0 * f->flow_completion_time);
            if(freq.find(fct) == freq.end())
                freq[fct] = 0;
            freq[fct]++;
        }
    }
    for(auto it = freq.begin(); it != freq.end(); it++)
        std::cout << it->first << " " << it->second << "\n";
}

void assign_flow_deadline(std::deque<Flow *> flows)
{
    ExponentialRandomVariable *nv_intarr = new ExponentialRandomVariable(params.avg_deadline);
    for(uint i = 0; i < flows.size(); i++)
    {
        Flow* f = flows[i];
        double rv = nv_intarr->value();
        f->deadline = f->start_time
            + std::max(topology->get_oracle_fct(f)/1000000.0 * 1.25, rv);
        //std::cout << f->start_time << " " << f->deadline << " " << topology->get_oracle_fct(f)/1000000 << " " << rv << "\n";
    }
}

void printQueueStatistics(Topology *topo) {
    double totalSentFromHosts = 0;

    uint64_t dropAt[4];
    uint64_t total_drop = 0;
    for (auto i = 0; i < 4; i++) {
        dropAt[i] = 0;
    }

    for (uint i = 0; i < topo->hosts.size(); i++) {
        int location = topo->hosts[i]->queue->getLocation();
        dropAt[location] += topo->hosts[i]->queue->getPacketDrop();
    }


    for (uint i = 0; i < topo->switches.size(); i++) {
        for (uint j = 0; j < topo->switches[i]->queues.size(); j++) {
            int location = topo->switches[i]->queues[j]->getLocation();
            dropAt[location] += topo->switches[i]->queues[j]->getPacketDrop();
        }
    }

    for (int i = 0; i < 4; i++) {
        total_drop += dropAt[i];
    }

#if 0
    for (int i = 0; i < 4; i++) {
        std::cout << "Hop:" << i 
            << " Drp:" << dropAt[i] 
            << "("  
            << (int)((double)dropAt[i]/total_drop * 100) 
            << "%) ";
    }
#endif

    for (auto h = (topo->hosts).begin(); h != (topo->hosts).end(); h++) {
        totalSentFromHosts += (*h)->queue->getSizeDepartures();
    }

    std::cout << "# of drops: " << total_drop << std::endl;

    std::cout << " Overall % of data sent dropped:"
        << std::setprecision(2)
        << total_drop * 1460.0 / totalSentFromHosts
        << "\n";

    double totalSentToHosts = 0;
    for (auto tor = (topo->switches).begin(); tor != (topo->switches).end(); tor++) {
        for (auto q = ((*tor)->queues).begin(); q != ((*tor)->queues).end(); q++) {
            if ((*q)->getRate() == params.bandwidth)
                totalSentToHosts += (*q)->getSizeDepartures();
        }
    }

    double dead_bytes = totalSentFromHosts - totalSentToHosts;
    double total_bytes = 0;
    for (auto f = flows_to_schedule.begin(); f != flows_to_schedule.end(); f++) {
        total_bytes += (*f)->size;
    }

    double simulation_time = current_time - start_time;
    double utilization = (totalSentFromHosts * 8.0 / 144.0) / simulation_time;
    double dst_utilization = (totalSentToHosts * 8.0 / 144.0) / simulation_time;

    std::cout
        << "DeadPackets " << 100.0 * (dead_bytes/total_bytes) << std::endl
        << "% DuplicatedPackets " 
        << 100.0 * duplicated_packets_received * 1460.0 / total_bytes << std::endl
        //<< "% Utilization " << utilization / 10000000000 * 100 << std::endl
        //<< "% " << dst_utilization / 10000000000 * 100 << std::endl
        << "%\n";
}

void run_experiment(int argc, char **argv, uint32_t exp_type) {
    if (argc < 3) {
        std::cout << "Usage: <exe> exp_type conf_file" << std::endl;
        return;
    }

    std::string conf_filename(argv[2]);
    std::cout << "Reading experiment parameters" << std::endl;
    read_experiment_parameters(conf_filename, exp_type);
    std::cout << "Number hosts: 144 agg switches: 9 and core switches: 4" << std::endl;
    params.num_hosts = 144;
    params.num_agg_switches = 9;
    params.num_core_switches = 4;
    
    if (params.flow_type == FASTPASS_FLOW) {
        throw std::runtime_error("FASTPASS_FLOW");
        topology = new FastpassTopology(params.num_hosts, params.num_agg_switches,
                params.num_core_switches, params.bandwidth, params.queue_type);
    }
    else if (params.big_switch) {
        throw std::runtime_error("big switch");
        topology = new BigSwitchTopology(params.num_hosts,
                params.bandwidth, params.queue_type);
    } 
    else {
        topology = new PFabricTopology(params.num_hosts, params.num_agg_switches,
                params.num_core_switches, params.bandwidth, params.queue_type);
    }

    uint32_t num_flows = params.num_flows_to_run;

    FlowGenerator *fg;
    if (params.use_flow_trace) {
        std::cout << "Generating traffic with FlowReader" << std::endl;
        std::cout << "Filename: " << params.cdf_or_flow_trace << std::endl;
        fg = new FlowReader(num_flows, topology, params.cdf_or_flow_trace);
        fg->make_flows();
    }
    else if (params.interarrival_cdf != "none") {
        std::cout << "Generating traffic with CustomCDFFlowGenerator" << std::endl;
        fg = new CustomCDFFlowGenerator(num_flows, topology,
                params.cdf_or_flow_trace, params.interarrival_cdf);
        fg->make_flows();
    }
    else if (params.permutation_tm != 0) {
        std::cout << "Generating traffic with PermutationTM" << std::endl;
        fg = new PermutationTM(num_flows, topology, params.cdf_or_flow_trace);
        fg->make_flows();
    }
    else {
        if (params.traffic_imbalance < 0.01) {
            std::cout << "Generating flows wiht Poisson generator" << std::endl;
            fg = new PoissonFlowGenerator(num_flows, topology, params.cdf_or_flow_trace);
            fg->make_flows();
        }
        else {
            // TODO skew flow gen not yet implemented, need to move to FlowGenerator
            assert(false);
            //generate_flows_to_schedule_fd_with_skew(params.cdf_or_flow_trace, num_flows, topology);
        }
    }

    if (params.deadline) {
        assign_flow_deadline(flows_to_schedule);
    }

    std::deque<Flow*> flows_sorted = flows_to_schedule;

    struct FlowComparator {
        bool operator() (Flow* a, Flow* b) {
            return a->start_time < b->start_time;
        }
    } fc;

    std::cout << "Sorting flows" << std::endl;
    std::sort (flows_sorted.begin(), flows_sorted.end(), fc);

    for (uint32_t i = 0; i < flows_sorted.size(); i++) {
        Flow* f = flows_sorted[i];
        if (exp_type == GEN_ONLY) {
            std::cout << f->id << " " << f->size << " "
                << f->src->id << " " << f->dst->id << " " << 1e6*f->start_time << "\n";
        }
        else {
            flow_arrivals.push_back(new FlowArrivalEvent(f->start_time, f));
        }
    }

    if (exp_type == GEN_ONLY) {
        return;
    }

    add_to_event_queue(new LoggingEvent((flows_sorted.front())->start_time));

    std::cout 
        << "Running " << num_flows 
        << " Flows\nCDF_File " << params.cdf_or_flow_trace 
        << "\nBandwidth " << params.bandwidth/1e9 
        << "\nQueueSize " << params.queue_size 
        << "\nCutThrough " << params.cut_through 
        << "\nFlowType " << params.flow_type 
        << "\nQueueType " << params.queue_type 
        << "\nInit CWND " << params.initial_cwnd 
        << "\nMax CWND " << params.max_cwnd 
        << "\nRtx Timeout " << params.retx_timeout_value
        << "\nload_balancing (0: pkt)" << params.load_balancing 
        << std::endl;

    if (params.flow_type == FASTPASS_FLOW) {
        dynamic_cast<FastpassTopology*>(topology)->arbiter->start_arbiter();
    }

    // 
    // everything before this is setup; everything after is analysis
    //
    std::cout << "Running scenario" << std::endl;

    std::cout
        << "flow>id" << " " 
        << "flow->size" << " " 
        << "flow->src->id" << " " 
        << "flow->dst->id" << " " 
        << "flow->start_time" << " "                                                          
        << "flow->finish_time" << " " 
        << "flow->flow_completion_time" << " " 
        << "topology->get_oracle_fct(flow)" << " " 
        << "slowdown" << " " 
        << "flow->total_pkt_sent" << "/" << "(flow->size/flow->mss)" << "//" << "flow->received_count" << " " 
        << "flow->data_pkt_drop" << "/" << "flow->ack_pkt_drop" << "/" << "flow->pkt_drop" << " " 
        << "(flow->first_byte_send_time - flow->start_time)" << " " 
        << std::endl;


    run_scenario();
    std::cout << "scenario done" << std::endl;

    //write_flows_to_file(flows_sorted, "flow.tmp");

    Stats slowdown, inflation, fct, oracle_fct,
          first_send_time, slowdown_0_100, slowdown_100k_10m, slowdown_10m_inf,
          data_pkt_sent, parity_pkt_sent, data_pkt_drop, parity_pkt_drop, deadline;

    std::map<unsigned, Stats*> slowdown_by_size, queuing_delay_by_size, capa_sent_by_size,
        fct_by_size, drop_rate_by_size, wait_time_by_size,
        first_hop_depart_by_size, last_hop_depart_by_size,
        capa_waste_by_size, log_slow_down_in_bytes;

    std::cout << std::setprecision(4);

    for (uint32_t i = 0; i < flows_sorted.size(); i++) {
        Flow *f = flows_to_schedule[i];
        validate_flow(f);
        if(!f->finished) {
            std::cout 
                << "unfinished flow " 
                << "size:" << f->size 
                << " id:" << f->id 
                << " next_seq:" << f->next_seq_no 
                << " recv:" << f->received_bytes  
                << " src:" << f->src->id 
                << " dst:" << f->dst->id 
                << "\n";
        }

        double slow = 1000000.0 * f->flow_completion_time / topology->get_oracle_fct(f);
        int meet_deadline = f->deadline > f->finish_time?1:0;
        if (slowdown_by_size.find(f->size_in_pkt) == slowdown_by_size.end()) {
            slowdown_by_size[f->size_in_pkt] = new Stats();
            queuing_delay_by_size[f->size_in_pkt] = new Stats();
            fct_by_size[f->size_in_pkt] = new Stats();
            drop_rate_by_size[f->size_in_pkt] = new Stats();
            wait_time_by_size[f->size_in_pkt] = new Stats();
            capa_sent_by_size[f->size_in_pkt] = new Stats();
            first_hop_depart_by_size[f->size_in_pkt] = new Stats();
            last_hop_depart_by_size[f->size_in_pkt] = new Stats();
            capa_waste_by_size[f->size_in_pkt] = new Stats();
        }
        int log_flow_size_in_bytes = (int)log10(f->size);
        if (log_slow_down_in_bytes.find(log_flow_size_in_bytes) == log_slow_down_in_bytes.end()) {
            log_slow_down_in_bytes[log_flow_size_in_bytes] = new Stats();
        }

        log_slow_down_in_bytes[log_flow_size_in_bytes]->input_data(slow);
        slowdown_by_size[f->size_in_pkt]->input_data(slow);
        queuing_delay_by_size[f->size_in_pkt]->input_data(f->get_avg_queuing_delay_in_us());
        fct_by_size[f->size_in_pkt]->input_data(f->flow_completion_time * 1000000);
        drop_rate_by_size[f->size_in_pkt]->input_data((double)(f->data_pkt_drop)/f->total_pkt_sent);
        wait_time_by_size[f->size_in_pkt]->input_data(f->first_byte_send_time - f->start_time);
        first_hop_depart_by_size[f->size_in_pkt]->input_data(f->first_hop_departure);
        last_hop_depart_by_size[f->size_in_pkt]->input_data(f->last_hop_departure);
        if (params.flow_type == CAPABILITY_FLOW) {
            capa_sent_by_size[f->size_in_pkt]->input_data(((CapabilityFlow*)f)->capability_count);
            capa_waste_by_size[f->size_in_pkt]->input_data(((CapabilityFlow*)f)->capability_waste_count);
        }

        if (params.deadline)
            deadline += meet_deadline;

        slowdown += slow;
        if (f->size < 100 * 1024)
            slowdown_0_100 += slow;
        else if (f->size < 10 * 1024 * 1024)
            slowdown_100k_10m += slow;
        else
            slowdown_10m_inf += slow;
        inflation += (double)f->total_pkt_sent / (f->size/f->mss);
        fct += (1000000.0 * f->flow_completion_time);
        oracle_fct += topology->get_oracle_fct(f);

        data_pkt_sent += std::min(f->size_in_pkt, f->total_pkt_sent);
        parity_pkt_sent += std::max(0U, (f->total_pkt_sent - f->size_in_pkt));
        data_pkt_drop += f->data_pkt_drop;
        parity_pkt_drop += std::max(0, f->pkt_drop - f->data_pkt_drop);
    }

    std::cout 
        << "Average FCT: "   << fct.avg() << std::endl
        << " 99\% FCT: "       << fct.get_percentile(0.99) << std::endl
        << " 99.9\% FCT: "   << fct.get_percentile(0.999) << std::endl
        << " MeanSlowdown: "  << slowdown.avg() << std::endl
        << " MeanInflation: " << inflation.avg() << std::endl
        << " NFCT: " << fct.total() / oracle_fct.total();

    if (params.deadline) {
        std::cout << " DL:" << deadline.avg();
    }

    std::cout << std::endl;

#if 0
    std::cout << "Slowdown Log Scale: ";
    for (auto it = log_slow_down_in_bytes.begin(); it != log_slow_down_in_bytes.end(); ++it) {
        std::cout << pow(10, it->first) << ":" << it->second->avg() << " ";
    }
    std::cout << "\n";
#endif

    //int i = 0;
    //for (auto it = slowdown_by_size.begin();
    //        it != slowdown_by_size.end() && i < 6; ++it, ++i) {
    //    unsigned key = it->first;
    //    std::cout 
    //        << key << ": Sl:" << it->second->avg() 
    //        <<  " FCT:" << fct_by_size[it->first]->avg() 
    //        << " QD:" << queuing_delay_by_size[it->first]->avg() 
    //        << "(" << queuing_delay_by_size[it->first]->sd() << ")" 
    //        << " Drp:" << drop_rate_by_size[it->first]->avg() 
    //        << " Wt:" << wait_time_by_size[it->first]->avg()*1000000;
    //    if (params.flow_type == CAPABILITY_FLOW)
    //        std::cout << " DC:" 
    //            <<  (capa_sent_by_size[it->first]->total() - 
    //                    first_hop_depart_by_size[it->first]->total()) / 
    //            capa_sent_by_size[it->first]->total();
    //    
    //    std::cout << " DP:" << 
    //        (first_hop_depart_by_size[it->first]->total()
    //         - last_hop_depart_by_size[it->first]->total()) 
    //        / first_hop_depart_by_size[it->first]->total();
    //    
    //    std::cout << " WST:" 
    //        << capa_waste_by_size[it->first]->total()
    //        / capa_sent_by_size[it->first]->total();
    //    
    //    std::cout << "    ";
    //}

    std::cout 
        << " [0,100k]avg: " << slowdown_0_100.avg() 
        << " [0,100k]99p: " << slowdown_0_100.get_percentile(0.99)
        << " [100k,10m]avg: " << slowdown_100k_10m.avg() 
        << " [100k,10m]99p: " << slowdown_100k_10m.get_percentile(0.99)
        << " [10m,inf]avg: " << slowdown_10m_inf.avg() 
        << " [10m,inf]99p: " << slowdown_10m_inf.get_percentile(0.99)
        << "\n";

    printQueueStatistics(topology);

    std::cout 
        << "Data Pkt Drop Rate: " << data_pkt_drop.total() / data_pkt_sent.total() 
        << " Parity Drop Rate:" << parity_pkt_drop.total()/parity_pkt_sent.total() 
        << "\n";

    //debug_flow_stats(flows_to_schedule);

    std::cout << params.param_str << "\n";

    //cleanup
    
    delete fg;
}

