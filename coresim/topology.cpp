#include "topology.h"

extern DCExpParams params;

/*
   uint32_t num_hosts = 144;
   uint32_t num_agg_switches = 9;
   uint32_t num_core_switches = 4;
   */
Topology::Topology() {}

// PUT this somewhere more global (?)
const auto HOSTS_PER_RACK = 16;

/*
 * PFabric topology with 144 hosts (16, 9, 4)
 */
PFabricTopology::PFabricTopology(
        uint32_t num_hosts, 
        uint32_t num_agg_switches,
        uint32_t num_core_switches, 
        double bandwidth,
        uint32_t queue_type
        ) : Topology () {
    uint32_t hosts_per_agg_switch = num_hosts / num_agg_switches;

    this->num_hosts = num_hosts;
    this->num_agg_switches = num_agg_switches;
    this->num_core_switches = num_core_switches;

    //Capacities
    double c1 = bandwidth;
    double c2 = hosts_per_agg_switch * bandwidth / num_core_switches;

    // Create Hosts
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts.push_back(Factory::get_host(i, c1, queue_type, params.host_type)); 
    }

    // Create aggregation Switches
    for (uint32_t i = 0; i < num_agg_switches; i++) {
        AggSwitch* sw = new AggSwitch(i, hosts_per_agg_switch, c1, num_core_switches, c2, queue_type);
        agg_switches.push_back(sw); // TODO make generic
        switches.push_back(sw);
    }

    // create core switches
    for (uint32_t i = 0; i < num_core_switches; i++) {
        CoreSwitch* sw = new CoreSwitch(i + num_agg_switches, num_agg_switches, c2, queue_type);
        core_switches.push_back(sw);
        switches.push_back(sw);
    }

    //Connect host queues
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts[i]->queue->set_src_dst(hosts[i], agg_switches[i / HOSTS_PER_RACK]);
        //std::cout << "Linking Host " << i << " to Agg " << i/16 << "\n";
    }

    // For agg switches -- REMAINING
    for (uint32_t i = 0; i < num_agg_switches; i++) {
        // Queues to Hosts
        for (uint32_t j = 0; j < hosts_per_agg_switch; j++) { // TODO make generic
            Queue *q = agg_switches[i]->queues[j];
            q->set_src_dst(agg_switches[i], hosts[i * HOSTS_PER_RACK + j]);
            //std::cout << "Linking Agg " << i << " to Host" << i * 16 + j << "\n";
        }
        // Queues to Core
        for (uint32_t j = 0; j < num_core_switches; j++) {
            Queue *q = agg_switches[i]->queues[j + HOSTS_PER_RACK];
            q->set_src_dst(agg_switches[i], core_switches[j]);
            //std::cout << "Linking Agg " << i << " to Core" << j << "\n";
        }
    }

    //For core switches -- PERFECT
    for (uint32_t i = 0; i < num_core_switches; i++) {
        for (uint32_t j = 0; j < num_agg_switches; j++) {
            Queue *q = core_switches[i]->queues[j];
            q->set_src_dst(core_switches[i], agg_switches[j]);
        }
    }
}

bool nodes_same_rack(Node* n1, Node* n2) {
    return (n1->id / HOSTS_PER_RACK) == (n2->id / HOSTS_PER_RACK);
}

Queue *PFabricTopology::get_next_hop(Packet *p, Queue *q) {
    if (q->getDst()->type == HOST) {
        return nullptr; // Packet Arrival
    }

    // At host level
    if (q->getSrc()->type == HOST) {
        assert (p->src->id == q->getSrc()->id);

        if (nodes_same_rack(p->src, p->dst)) {
            Switch* sw_dst = dynamic_cast<Switch*>(q->getDst());
            return sw_dst->queues[p->dst->id % HOSTS_PER_RACK];
        } else {
            uint32_t hash_port = 0;
            if (params.load_balancing == 0) {
                hash_port = q->getSprayCounter() % 4;
                q->incSprayCounter();
            }
            else if (params.load_balancing == 1)
                hash_port = (p->src->id + p->dst->id + p->flow->id) % 4; // ECMP

            Switch* sw_dst = dynamic_cast<Switch*>(q->getDst());
            return sw_dst->queues[HOSTS_PER_RACK + hash_port];
        }
    }

    // At switch level
    if (q->getSrc()->type == SWITCH) {
        Switch* sw_src = dynamic_cast<Switch*>(q->getSrc());
        Switch* sw_dst = dynamic_cast<Switch*>(q->getDst());

        switch (sw_src->switch_type) {
            case AGG_SWITCH:
                return sw_dst->queues[p->dst->id / HOSTS_PER_RACK];
            case CORE_SWITCH:
                return sw_dst->queues[p->dst->id % HOSTS_PER_RACK];
            default:
                throw std::runtime_error("Wrong switch type");
        };
    }

    throw std::runtime_error("Error");
}


double PFabricTopology::get_oracle_fct(Flow *f) {
    
    int num_hops = 4;
    if (nodes_same_rack(f->src, f->dst)) {
        num_hops = 2;
    }

    // XXX Check this!
    double propagation_delay = 0;
    if (params.ddc != 0) { 
        if (num_hops == 2) {
            propagation_delay = 0.440;
        }
        if (num_hops == 4) {
            propagation_delay = 2.040;
        }
    }
    else {
        throw std::runtime_error("Wrong ddc config");
        propagation_delay = 2 * 1000000.0 * num_hops * f->src->queue->getPropagationDelay();
    }
    
    uint32_t np = ceil(f->size / params.mss); // TODO: Must be a multiple of 1460
    double bandwidth = f->src->queue->getRate() / 1000000.0; // rate per us
    double transmission_delay = 0;
    if (params.cut_through) {
        transmission_delay = 
            (
                np * (params.mss + params.hdr_size)
                + 1 * params.hdr_size
                + 2.0 * params.hdr_size // ACK has to travel two hops
            ) * 8.0 / bandwidth;
        if (num_hops == 4) {
            //1 packet and 1 ack
            transmission_delay += 2 * (2 * params.hdr_size) * 8.0 / (4 * bandwidth);
        }
    }
    else {
        transmission_delay = 
            (
                (np + 1) * (params.mss + params.hdr_size) 
                + 2.0 * params.hdr_size // ACK has to travel two hops
            ) * 8.0 / bandwidth;
        if (num_hops == 4) {
            //1 packet and 1 ack
            transmission_delay += 2 * (params.mss + 2*params.hdr_size) * 8.0 / (4 * bandwidth); 
            //TODO: 4 * bw is not right.
        }
    }
    return (propagation_delay + transmission_delay); //us
}


/*
 *BigSwitchTopology  with 144 hosts
 */
BigSwitchTopology::BigSwitchTopology(
        uint32_t num_hosts, 
        double bandwidth, 
        uint32_t queue_type
        ) : Topology () {
    this->num_hosts = num_hosts;
    double c1 = bandwidth;

    // Create Hosts
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts.push_back(Factory::get_host(i, c1, queue_type, params.host_type));
    }

    the_switch = new CoreSwitch(0, num_hosts, c1, queue_type);
    this->switches.push_back(the_switch);

    assert(this->switches.size() == 1);

    //Connect host queues
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts[i]->queue->set_src_dst(hosts[i], the_switch);
        Queue *q = the_switch->queues[i];
        q->set_src_dst(the_switch, hosts[i]);
    }
}

Queue* BigSwitchTopology::get_next_hop(Packet *p, Queue *q) {
    if (q->getDst()->type == HOST) {
        assert(p->dst->id == q->getDst()->id);
        return nullptr; // Packet Arrival
    }

    // At host level
    if (q->getSrc()->type == HOST) { // Same Rack or not
        assert (p->src->id == q->getSrc()->id);
        return the_switch->queues[p->dst->id];
    }

    assert(false);
}

double BigSwitchTopology::get_oracle_fct(Flow *f) {
    double propagation_delay = 2 * 1000000.0 * 2 * f->src->queue->getPropagationDelay(); //us

    uint32_t np = ceil(f->size / params.mss); // TODO: Must be a multiple of 1460
    double bandwidth = f->src->queue->getRate() / 1000000.0; // For us
    double transmission_delay;
    if (params.cut_through) {
        transmission_delay = 
            (
                np * (params.mss + params.hdr_size)
                + 1 * params.hdr_size
                + 2.0 * params.hdr_size // ACK has to travel two hops
            ) * 8.0 / bandwidth;
    }
    else {
        transmission_delay = ((np + 1) * (params.mss + params.hdr_size) 
                + 2.0 * params.hdr_size) // ACK has to travel two hops
            * 8.0 / bandwidth;
    }
    return (propagation_delay + transmission_delay); //us
}

