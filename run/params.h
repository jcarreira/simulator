#ifndef PARAMS_H
#define PARAMS_H

#include <string>
#include <fstream>

class DCExpParams {
    public:
        std::string param_str;

        uint32_t initial_cwnd = 0;
        uint32_t max_cwnd = 0;
        double retx_timeout_value = 0;
        uint32_t mss = 0;
        uint32_t hdr_size = 0;
        uint32_t queue_size = 0;
        uint32_t queue_type = 1;
        uint32_t flow_type = 1;
        uint32_t load_balancing = 0; //0 per pkt, 1 per flow

        double propagation_delay;
        double bandwidth;

        uint32_t num_flows_to_run;
        double end_time;
        std::string cdf_or_flow_trace;
        uint32_t cut_through;
        uint32_t mean_flow_size;


        uint32_t num_hosts;
        uint32_t num_agg_switches;
        uint32_t num_core_switches;
        uint32_t preemptive_queue;
        uint32_t big_switch;
        uint32_t host_type;
        double traffic_imbalance;
        double load;

        double reauth_limit;

        double magic_trans_slack;
        uint32_t magic_delay_scheduling;
        uint32_t magic_inflate;

        uint32_t use_flow_trace;
        uint32_t smooth_cdf;
        uint32_t burst_at_beginning;
        double capability_timeout;
        double capability_resend_timeout;
        uint32_t capability_initial;
        uint32_t capability_window;
        uint32_t capability_prio_thresh;
        double capability_window_timeout;
        uint32_t capability_third_level;
        uint32_t capability_fourth_level;

        uint32_t ddc;
        double ddc_cpu_ratio;
        double ddc_mem_ratio;
        double ddc_disk_ratio;
        uint32_t ddc_normalize; //0: sender send, 1: receiver side, 2: both
        uint32_t ddc_type;

        uint32_t deadline;
        uint32_t schedule_by_deadline;
        double avg_deadline;
        std::string interarrival_cdf;
        uint32_t num_host_types;

        double fastpass_epoch_time;

        uint32_t permutation_tm;

        uint32_t dctcp_mark_thresh;
        //uint32_t dctcp_delayed_ack_freq;
        
        uint32_t use_shared_queue = 0;
        uint32_t agg_queue_size = 0;
        uint32_t core_queue_size = 0;

        uint32_t log_fct = 0;
        std::string fct_filename = "";

        struct {
            char     use = 0;
            uint32_t threshold = 0;     
        } flow_classes;

        struct {
            double alpha_back = 1.0/2.0;
            double alpha_prio = 2;
            double alpha = 1.0/32;
            uint32_t track_queue = 10000000; // default is we don't track any queue
            uint32_t track_switch = 10000000; // default is we don't track any switch
            std::string log_file; // file with queue buffer occupancy
        } shared_queue;

        double get_full_pkt_tran_delay(uint32_t size_in_byte = 1500)
        {
            return size_in_byte * 8 / this->bandwidth;
        }

};


#define CAPABILITY_MEASURE_WASTE false
#define CAPABILITY_NOTIFY_BLOCKING false
#define CAPABILITY_HOLD true

//#define FASTPASS_EPOCH_TIME 0.000010
#define FASTPASS_EPOCH_PKTS 8

void read_experiment_parameters(std::string conf_filename, uint32_t exp_type); 

/* General main function */
#define DEFAULT_EXP 1
#define GEN_ONLY 2

#define INFINITESIMAL_TIME 0.000000000001

#endif
