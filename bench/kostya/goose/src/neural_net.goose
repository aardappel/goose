// Etc::NeuralNet: a 2-10-1 sigmoid network learning XOR by backpropagation
// with momentum; the checksum is taken over its four outputs afterwards.

namespace neural_net;

import std;
import math;
import bench;
import helper;

const INPUT_00: f64[2] = [0.0, 0.0];
const INPUT_01: f64[2] = [0.0, 1.0];
const INPUT_10: f64[2] = [1.0, 0.0];
const INPUT_11: f64[2] = [1.0, 1.0];
const TARGET_0: f64[1] = [0.0];
const TARGET_1: f64[1] = [1.0];

struct Synapse { weight: f64, prev_weight: f64, source_neuron: i64, dest_neuron: i64 }

fn new_synapse(source_neuron: i64, dest_neuron: i64) -> Synapse {
    let w = next_float(1.0) * 2.0 - 1.0;
    return Synapse { weight: w, prev_weight: w, source_neuron: source_neuron, dest_neuron: dest_neuron };
}

// Neurons are array elements, which can't hold growable arrays, so the
// synapse lists are limited arrays as long as the widest layer.
let MAX_SYNAPSES = 10;

struct Neuron {
    threshold: f64,
    prev_threshold: f64,
    output: f64,
    error: f64,
    synapses_in: i64[..MAX_SYNAPSES],
    synapses_out: i64[..MAX_SYNAPSES],
}

let LEARNING_RATE = 1.0;
let MOMENTUM = 0.3;

fn new_neuron() -> Neuron {
    let t = next_float(1.0) * 2.0 - 1.0;
    return Neuron { threshold: t, prev_threshold: t, output: 0.0, error: 0.0,
                    synapses_in: [], synapses_out: [] };
}

fn derivative(n: Neuron&) -> f64 { n.output * (1.0 - n.output) }

fn calculate_output(n: Neuron&, sources: Neuron[:], synapses: Synapse[:]) {
    var activation = 0.0;
    for synapse_idx in n.synapses_in {
        let synapse .= synapses[synapse_idx];
        activation += synapse.weight * sources[synapse.source_neuron].output;
    }
    activation -= n.threshold;
    n.output = 1.0 / (1.0 + exp(-activation));
}

fn update_weights(n: Neuron&, rate: f64, sources: Neuron[:], synapses: Synapse[:]) {
    for synapse_idx in n.synapses_in {
        let synapse .= synapses[synapse_idx];
        let source_output = sources[synapse.source_neuron].output;
        let temp_weight = synapse.weight;
        synapse.weight += (rate * LEARNING_RATE * n.error * source_output)
                        + (MOMENTUM * (synapse.weight - synapse.prev_weight));
        synapse.prev_weight = temp_weight;
    }
    let temp_threshold = n.threshold;
    n.threshold += (rate * LEARNING_RATE * n.error * -1.0)
                 + (MOMENTUM * (n.threshold - n.prev_threshold));
    n.prev_threshold = temp_threshold;
}

struct NeuralNetwork {
    input_layer: Neuron[],
    hidden_layer: Neuron[],
    output_layer: Neuron[],
    synapses: Synapse[>..],
}

fn new_layer(n: i64) -> Neuron[>..] {
    var layer: Neuron[>..] = [];
    for i in n { layer.push(new_neuron()); }
    return layer;
}

fn new_network(inputs: i64, hidden: i64, outputs: i64) -> NeuralNetwork {
    var input_layer = new_layer(inputs);
    var hidden_layer = new_layer(hidden);
    var output_layer = new_layer(outputs);
    var synapses: Synapse[>..] = [];

    connect(input_layer, hidden_layer, synapses);
    connect(hidden_layer, output_layer, synapses);

    return NeuralNetwork {
        input_layer: input_layer,
        hidden_layer: hidden_layer,
        output_layer: output_layer,
        synapses: synapses,
    };
}

fn connect(sources: Neuron[:], destinations: Neuron[:], synapses: Synapse[>..]&) {
    for &source, source_idx in sources {
        for &dest, dest_idx in destinations {
            let synapse_idx = synapses.len;
            synapses.push(new_synapse(source_idx, dest_idx));
            source.synapses_out.push(synapse_idx);
            dest.synapses_in.push(synapse_idx);
        }
    }
}

fn train(net: NeuralNetwork&, inputs: f64[:], targets: f64[:]) {
    net.feed_forward(inputs);

    let RATE = 0.3;

    for &neuron, i in net.output_layer {
        neuron.error = (targets[i] - neuron.output) * neuron.derivative();
        neuron.update_weights(RATE, net.hidden_layer, net.synapses);
    }

    for &neuron in net.hidden_layer {
        var sum = 0.0;
        for synapse_idx in neuron.synapses_out {
            let synapse .= net.synapses[synapse_idx];
            sum += synapse.prev_weight * net.output_layer[synapse.dest_neuron].error;
        }
        neuron.error = sum * neuron.derivative();
        neuron.update_weights(RATE, net.input_layer, net.synapses);
    }
}

fn feed_forward(net: NeuralNetwork&, inputs: f64[:]) {
    for &neuron, i in net.input_layer { neuron.output = inputs[i]; }
    for &neuron in net.hidden_layer { neuron.calculate_output(net.input_layer, net.synapses); }
    for &neuron in net.output_layer { neuron.calculate_output(net.hidden_layer, net.synapses); }
}

fn current_outputs(net: NeuralNetwork&) -> f64[>..] {
    return net.output_layer.map() { it.output };
}

fn run(b: Bench&) -> u32 {
    var xor_net = new_network(2, 10, 1);

    measure(b) {
        for i in 1000 {
            xor_net.train(INPUT_00, TARGET_0);
            xor_net.train(INPUT_10, TARGET_1);
            xor_net.train(INPUT_01, TARGET_1);
            xor_net.train(INPUT_11, TARGET_0);
        }
    };

    var all_outputs: f64[>..] = [];
    xor_net.feed_forward(INPUT_00);
    all_outputs.append(xor_net.current_outputs());
    xor_net.feed_forward(INPUT_01);
    all_outputs.append(xor_net.current_outputs());
    xor_net.feed_forward(INPUT_10);
    all_outputs.append(xor_net.current_outputs());
    xor_net.feed_forward(INPUT_11);
    all_outputs.append(xor_net.current_outputs());
    return checksum_f64(sum(all_outputs));
}
