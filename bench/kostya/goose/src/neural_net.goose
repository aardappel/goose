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

// The network holds its neurons and synapses, and they refer to each other
// by 4-byte offsets within it.
//
// A synapse is an object of its own, which the lists of the neurons on both
// sides of it refer to, as in the other versions, for the sake of comparing
// like with like. This is not idiomatic Goose: we would keep each synapse
// inline in the neuron it feeds, which runs this benchmark about 1.1x faster.
struct Synapse {
    weight: f64,
    prev_weight: f64,
    source_neuron: Neuron&<u32>,
    dest_neuron: Neuron&<u32>,
}

// A value holding such links is built where it stays (it can't be copied
// there), so a synapse is made in the network's array of them.
fn new_synapse(synapses: Synapse[..]&, source_neuron: Neuron&, dest_neuron: Neuron&) -> Synapse& {
    let w = next_float(1.0) * 2.0 - 1.0;
    return synapses.push(Synapse { weight: w, prev_weight: w,
                                   source_neuron: source_neuron, dest_neuron: dest_neuron });
}

// Neurons are array elements, which can't hold growable arrays, so the
// synapse lists are limited arrays as long as the widest layer.
let MAX_SYNAPSES = 10;

struct Neuron {
    threshold: f64,
    prev_threshold: f64,
    output: f64,
    error: f64,
    synapses_in: (Synapse&<u32>)[..MAX_SYNAPSES],
    synapses_out: (Synapse&<u32>)[..MAX_SYNAPSES],
}

let LEARNING_RATE = 1.0;
let MOMENTUM = 0.3;

fn new_neuron() -> Neuron {
    let t = next_float(1.0) * 2.0 - 1.0;
    return Neuron { threshold: t, prev_threshold: t, output: 0.0, error: 0.0,
                    synapses_in: [], synapses_out: [] };
}

fn calculate_output(n: Neuron&) {
    var activation = 0.0;
    for synapse in n.synapses_in {
        activation += synapse.weight * synapse.source_neuron.output;
    }
    activation -= n.threshold;
    n.output = 1.0 / (1.0 + exp(-activation));
}

fn derivative(n: Neuron&) -> f64 { n.output * (1.0 - n.output) }

fn output_train(n: Neuron&, rate: f64, target: f64) {
    n.error = (target - n.output) * n.derivative();
    n.update_weights(rate);
}

fn hidden_train(n: Neuron&, rate: f64) {
    var sum = 0.0;
    for synapse in n.synapses_out {
        sum += synapse.prev_weight * synapse.dest_neuron.error;
    }
    n.error = sum * n.derivative();
    n.update_weights(rate);
}

fn update_weights(n: Neuron&, rate: f64) {
    for &synapse in n.synapses_in {
        let temp_weight = synapse.weight;
        synapse.weight += (rate * LEARNING_RATE * n.error * synapse.source_neuron.output)
                        + (MOMENTUM * (synapse.weight - synapse.prev_weight));
        synapse.prev_weight = temp_weight;
    }
    let temp_threshold = n.threshold;
    n.threshold += (rate * LEARNING_RATE * n.error * -1.0)
                 + (MOMENTUM * (n.threshold - n.prev_threshold));
    n.prev_threshold = temp_threshold;
}

// The layers are sized when the network is made, and so is the array of its
// synapses, which a fully connected network knows the number of.
struct NeuralNetwork {
    input_layer: Neuron[],
    hidden_layer: Neuron[],
    output_layer: Neuron[],
    synapses: Synapse[..],
}

fn new_layer(n: i64) -> Neuron[>..] {
    var layer: Neuron[>..] = [];
    for i in n { layer.push(new_neuron()); }
    return layer;
}

fn new_network(inputs: i64, hidden: i64, outputs: i64) -> NeuralNetwork {
    var net = NeuralNetwork {
        input_layer: new_layer(inputs),
        hidden_layer: new_layer(hidden),
        output_layer: new_layer(outputs),
        synapses: [..inputs * hidden + hidden * outputs],
    };

    connect(net.input_layer, net.hidden_layer, net.synapses);
    connect(net.hidden_layer, net.output_layer, net.synapses);

    return net;
}

fn connect(sources: Neuron[:], destinations: Neuron[:], synapses: Synapse[..]&) {
    for &source in sources {
        for &dest in destinations {
            let synapse .= new_synapse(synapses, source, dest);
            source.synapses_out.push(synapse);
            dest.synapses_in.push(synapse);
        }
    }
}

fn train(net: NeuralNetwork&, inputs: f64[:], targets: f64[:]) {
    net.feed_forward(inputs);

    let RATE = 0.3;

    for &neuron, i in net.output_layer { neuron.output_train(RATE, targets[i]); }
    for &neuron in net.hidden_layer { neuron.hidden_train(RATE); }
}

fn feed_forward(net: NeuralNetwork&, inputs: f64[:]) {
    for &neuron, i in net.input_layer { neuron.output = inputs[i]; }
    for &neuron in net.hidden_layer { neuron.calculate_output(); }
    for &neuron in net.output_layer { neuron.calculate_output(); }
}

fn current_outputs(net: NeuralNetwork&) -> f64[>..] {
    var outputs: f64[>..] = [];
    for &neuron in net.output_layer { outputs.push(neuron.output); }
    return outputs;
}

// A network holds links into itself, so it is built where it stays, at the
// size it is made with: the benchmark makes its 2-10-1 network when it is
// made itself, rather than an empty one for prepare to replace.
struct NeuralNet { xor_net: NeuralNetwork }

fn new_neural_net() -> NeuralNet { NeuralNet { xor_net: new_network(2, 10, 1) } }

fn prepare(s: NeuralNet&) {}

fn run(s: NeuralNet&, iteration_id: i64) {
    for i in 1000 {
        s.xor_net.train(INPUT_00, TARGET_0);
        s.xor_net.train(INPUT_10, TARGET_1);
        s.xor_net.train(INPUT_01, TARGET_1);
        s.xor_net.train(INPUT_11, TARGET_0);
    }
}

fn checksum(s: NeuralNet&) -> u32 {
    var all_outputs: f64[>..] = [];
    s.xor_net.feed_forward(INPUT_00);
    all_outputs.append(s.xor_net.current_outputs());
    s.xor_net.feed_forward(INPUT_01);
    all_outputs.append(s.xor_net.current_outputs());
    s.xor_net.feed_forward(INPUT_10);
    all_outputs.append(s.xor_net.current_outputs());
    s.xor_net.feed_forward(INPUT_11);
    all_outputs.append(s.xor_net.current_outputs());
    return checksum_f64(sum(all_outputs));
}
