// Etc::NeuralNet: a 2-10-1 sigmoid network learning XOR by backpropagation
// with momentum; the checksum is taken over its four outputs afterwards.

namespace neural_net;

import std;
import math;
import bench;
import helper;

let INPUTS = 2;
let HIDDEN = 10;
let OUTPUTS = 1;

// Layers are fully connected, so no neuron has more synapses on either side
// than the widest layer has neurons.
let MAX_LINKS = HIDDEN;

let LEARNING_RATE = 1.0;
let MOMENTUM = 0.3;
let RATE = 0.3;

// A synapse lives inline in the neuron it feeds, and links both of its
// neurons by reference; the neuron it comes from keeps a reference to it.
struct Synapse { weight: f64, prev_weight: f64, source: Neuron&, dest: Neuron& }

struct Neuron {
    threshold: f64,
    prev_threshold: f64,
    output: f64 = 0.0,
    error: f64 = 0.0,
    synapses_in: Synapse[..MAX_LINKS] = [],
    synapses_out: (Synapse&)[..MAX_LINKS] = [],
}

fn new_neuron() -> Neuron {
    let t = next_float(1.0) * 2.0 - 1.0;
    return Neuron { threshold: t, prev_threshold: t };
}

fn derivative(n: Neuron&) -> f64 { n.output * (1.0 - n.output) }

fn calculate_output(n: Neuron&) {
    var activation = 0.0;
    for s in n.synapses_in { activation += s.weight * s.source.output; }
    activation -= n.threshold;
    n.output = 1.0 / (1.0 + exp(-activation));
}

fn update_weights(n: Neuron&, rate: f64) {
    for &s in n.synapses_in {
        let temp_weight = s.weight;
        s.weight += (rate * LEARNING_RATE * n.error * s.source.output)
                  + (MOMENTUM * (s.weight - s.prev_weight));
        s.prev_weight = temp_weight;
    }
    let temp_threshold = n.threshold;
    n.threshold += (rate * LEARNING_RATE * n.error * -1.0)
                 + (MOMENTUM * (n.threshold - n.prev_threshold));
    n.prev_threshold = temp_threshold;
}

fn connect(sources: Neuron[:], dests: Neuron[:]) {
    for &source in sources {
        for &dest in dests {
            let w = next_float(1.0) * 2.0 - 1.0;
            let s .= dest.synapses_in.push(Synapse { weight: w, prev_weight: w, source: source, dest: dest });
            source.synapses_out.push(s);
        }
    }
}

fn run(b: Bench&) -> u32 {
    // A fill literal evaluates its value once, so each neuron then draws its
    // own random threshold, layer by layer.
    var input_layer: Neuron[INPUTS] = [Neuron { .. }; INPUTS];
    var hidden_layer: Neuron[HIDDEN] = [Neuron { .. }; HIDDEN];
    var output_layer: Neuron[OUTPUTS] = [Neuron { .. }; OUTPUTS];
    for &n in input_layer { n = new_neuron(); }
    for &n in hidden_layer { n = new_neuron(); }
    for &n in output_layer { n = new_neuron(); }
    connect(input_layer, hidden_layer);
    connect(hidden_layer, output_layer);

    fn feed_forward(inputs: f64[:]) {
        for &n, i in input_layer { n.output = inputs[i]; }
        for &n in hidden_layer { calculate_output(n); }
        for &n in output_layer { calculate_output(n); }
    }

    fn train(inputs: f64[:], targets: f64[:]) {
        feed_forward(inputs);
        for &n, i in output_layer {
            n.error = (targets[i] - n.output) * derivative(n);
            update_weights(n, RATE);
        }
        for &n in hidden_layer {
            var sum = 0.0;
            for s in n.synapses_out { sum += s.prev_weight * s.dest.error; }
            n.error = sum * derivative(n);
            update_weights(n, RATE);
        }
    }

    measure(b) {
        for i in 1000 {
            train([0.0, 0.0], [0.0]);
            train([1.0, 0.0], [1.0]);
            train([0.0, 1.0], [1.0]);
            train([1.0, 1.0], [0.0]);
        }
    };

    var sum = 0.0;
    for inputs in [[0.0, 0.0], [0.0, 1.0], [1.0, 0.0], [1.0, 1.0]] {
        feed_forward(inputs);
        for n in output_layer { sum += n.output; }
    }
    return checksum_f64(sum);
}
