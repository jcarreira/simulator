#include "params.h"

#include <assert.h>
#include <iostream>
#include <fstream>
#include <sstream>

DCExpParams params;

void remove_comment(std::string& line) {
    auto pos = line.find('#');
    if (pos != std::string::npos) {
        auto it = line.begin() + pos - 1;
        while (*it == ' ')
            it--;
        line.erase(it, line.end());
    }
}

/* Read parameters from a config file */
void read_experiment_parameters(std::string conf_filename, uint32_t exp_type) {
    std::ifstream input(conf_filename);
    std::string line;
    std::string key;
    params.interarrival_cdf = "none";
    params.permutation_tm = 0;
    params.hdr_size = 40;

    while (std::getline(input, line)) {
        std::istringstream lineStream(line);
        if (line.empty()) {
            continue;
        }

        lineStream >> key;

        // we support comments now
        if (key[0] == '#')
            continue;

        assert(key[key.length()-1] == ':');
        key = key.substr(0, key.length()-1);

        if (key == "init_cwnd") {
            lineStream >> params.initial_cwnd;
        }
        else if (key == "max_cwnd") {
            lineStream >> params.max_cwnd;
        }
        else if (key == "retx_timeout") {
            lineStream >> params.retx_timeout_value;
        }
        else if (key == "propagation_delay") {
            lineStream >> params.propagation_delay;
        }
        else if (key == "bandwidth") {
            lineStream >> params.bandwidth;
        }
        else if (key == "queue_type") {
            lineStream >> params.queue_type;
        }
        else if (key == "flow_type") {
            lineStream >> params.flow_type;
        }
        else if (key == "num_flow") {
            lineStream >> params.num_flows_to_run;
        }
        else if (key == "flow_trace") {
            lineStream >> params.cdf_or_flow_trace;
            std::cout << "cdf_or_flow_trace: " << params.cdf_or_flow_trace << std::endl;
        }
        else if (key == "cut_through") {
            lineStream >> params.cut_through;
        }
        else if (key == "mean_flow_size") {
            lineStream >> params.mean_flow_size;
        }
        else if (key == "load_balancing") {
            lineStream >> params.load_balancing;
        }
        else if (key == "preemptive_queue") {
            lineStream >> params.preemptive_queue;
        }
        else if (key == "big_switch") {
            lineStream >> params.big_switch;
        }
        else if (key == "host_type") {
            lineStream >> params.host_type;
        }
        else if (key == "imbalance") {
            lineStream >> params.traffic_imbalance;
        }
        else if (key == "load") {
            lineStream >> params.load;
        }
        else if (key == "traffic_imbalance") {
            lineStream >> params.traffic_imbalance;
        }
        else if (key == "reauth_limit") {
            lineStream >> params.reauth_limit;
        }
        else if (key == "magic_trans_slack") {
            lineStream >> params.magic_trans_slack;
        }
        else if (key == "magic_delay_scheduling") {
            lineStream >> params.magic_delay_scheduling;
        }
        else if (key == "capability_timeout") {
            lineStream >> params.capability_timeout;
        }
        else if (key == "use_flow_trace") {
            lineStream >> params.use_flow_trace;
        }
        else if (key == "smooth_cdf") {
            lineStream >> params.smooth_cdf;
        }
        else if (key == "burst_at_beginning") {
            lineStream >> params.burst_at_beginning;
        }
        else if (key == "capability_resend_timeout") {
            lineStream >> params.capability_resend_timeout;
        }
        else if (key == "capability_initial") {
            lineStream >> params.capability_initial;
        }
        else if (key == "capability_window") {
            lineStream >> params.capability_window;
        }
        else if (key == "capability_prio_thresh") {
            lineStream >> params.capability_prio_thresh;
        }
        else if (key == "capability_third_level") {
            lineStream >> params.capability_third_level;
        }
        else if (key == "capability_fourth_level") {
            lineStream >> params.capability_fourth_level;
        }
        else if (key == "capability_window_timeout") {
            lineStream >> params.capability_window_timeout;
        }
        else if (key == "ddc") {
            lineStream >> params.ddc;
        }
        else if (key == "ddc_cpu_ratio") {
            lineStream >> params.ddc_cpu_ratio;
        }
        else if (key == "ddc_mem_ratio") {
            lineStream >> params.ddc_mem_ratio;
        }
        else if (key == "ddc_disk_ratio") {
            lineStream >> params.ddc_disk_ratio;
        }
        else if (key == "ddc_normalize") {
            lineStream >> params.ddc_normalize;
        }
        else if (key == "ddc_type") {
            lineStream >> params.ddc_type;
        }
        else if (key == "deadline") {
            lineStream >> params.deadline;
        }
        else if (key == "schedule_by_deadline") {
            lineStream >> params.schedule_by_deadline;
        }
        else if (key == "avg_deadline") {
            lineStream >> params.avg_deadline;
        }
        else if (key == "magic_inflate") {
            lineStream >> params.magic_inflate;
        }
        else if (key == "interarrival_cdf") {
            lineStream >> params.interarrival_cdf;
        }
        else if (key == "num_host_types") {
            lineStream >> params.num_host_types;
        }
        else if (key == "permutation_tm") {
            lineStream >> params.permutation_tm;
        }
        else if (key == "dctcp_mark_thresh") {
            lineStream >> params.dctcp_mark_thresh;
        }
        else if (key == "hdr_size") {
            lineStream >> params.hdr_size;
            assert(params.hdr_size > 0);
        }
        // QUEUE Configuration
        else if (key == "queue_size") {
            lineStream >> params.queue_size;
        }
        else if (key == "agg_queue_size") {
            lineStream >> params.agg_queue_size;
        }
        else if (key == "core_queue_size") {
            lineStream >> params.core_queue_size;
        }
        else if (key == "use_shared_buffer") {
            lineStream >> params.use_shared_buffer;
        }
        else if (key == "shared_queue.alpha") {
            lineStream >> params.shared_queue.alpha;
        }
        else if (key == "shared_queue.alpha_prio") {
            lineStream >> params.shared_queue.alpha_prio;
        }
        else if (key == "shared_queue.alpha_back") {
            lineStream >> params.shared_queue.alpha_back;
        }
        // enables tracking buffer utilization of one specific queu
        else if (key == "shared_queue.track_queue") {
            lineStream >> params.shared_queue.track_queue;
        }
        else if (key == "shared_queue.track_switch") {
            lineStream >> params.shared_queue.track_switch;
        }
        else if (key == "shared_queue.log_file") {
            lineStream >> params.shared_queue.log_file;
        }
        else if (key == "flow_classes.use") {
            lineStream >> params.flow_classes.use;
        }
        else if (key == "flow_classes.threshold") {
            lineStream >> params.flow_classes.threshold;
        }
        else if (key == "log_fct") {
            lineStream >> params.log_fct;
        }
        else if (key == "fct_filename") {
            lineStream >> params.fct_filename;
        }
        else if (key == "nw_stack_delay") {
            lineStream >> params.nw_stack_delay;
        }
        else {
            std::cerr << "Unknown conf param: " << key << " in file: " << conf_filename << std::endl;
            exit(-1);
        }
        
        params.fastpass_epoch_time = 1500 * 8 * (FASTPASS_EPOCH_PKTS + 0.5) / params.bandwidth;

        remove_comment(line);

        params.param_str.append(line);
        params.param_str.append(", ");
    }

    params.mss = 1460;
}
