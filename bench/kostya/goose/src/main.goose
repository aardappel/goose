// LangArena in Goose: runs the benchmarks a configuration file lists, in its
// order, and prints the per-benchmark time and checksum verdict in the format
// every other language's runner prints.
//
//   benchmark [config.js] [name-substring]

import std;
import slice_pool;
import os;
import bench;
import helper;
import binarytrees;
import brainfuck_array;
import brainfuck_recursion;
import matmul;
import base64_encode;
import base64_decode;
import json_generate;
import json_parse_dom;
import json_parse_mapping;
import csv_parse;
import sieve;
import text_raytracer;
import neural_net;
import cache_simulation;
import game_of_life;
import words;
import distance;
import log_parser;
import template;
import sort;
import graph_path;
import buffer_hash;
import calculator_ast;
import calculator_interpreter;
import maze;
import fannkuchredux;
import mandelbrot;
import nbody;
import spectralnorm;
import bwt;
import huffman;
import arith;
import lzw;

// The checksum of one benchmark, or false for a name with no implementation.
fn dispatch(b: Bench&) -> u32, bool {
    let n = b.name;
    if n == "Binarytrees::Obj" { var s = binarytrees::new_binarytrees_obj(); return execute(b, s, binarytrees::prepare, binarytrees::run, binarytrees::checksum), true; }
    if n == "Binarytrees::Arena" { var s = binarytrees::new_binarytrees_arena(); return execute(b, s, binarytrees::prepare, binarytrees::run, binarytrees::checksum), true; }
    if n == "Brainfuck::Array" {
        var s = brainfuck_array::new_brainfuck_array();
        return execute_with_warmup(b, s, brainfuck_array::prepare, brainfuck_array::warmup,
                                   brainfuck_array::run, brainfuck_array::checksum), true;
    }
    if n == "Brainfuck::Recursion" {
        var s = brainfuck_recursion::new_brainfuck_recursion();
        return execute_with_warmup(b, s, brainfuck_recursion::prepare, brainfuck_recursion::warmup,
                                   brainfuck_recursion::run, brainfuck_recursion::checksum), true;
    }
    if n == "Matmul::Single" {
        var pool = slice_pool<f64> {};
        var s = matmul::new_matmul1t(pool);
        return execute(b, s, matmul::prepare, matmul::run, matmul::checksum), true;
    }
    if n == "Matmul::T4" {
        var pool = slice_pool<f64> {};
        var s = matmul::new_matmul4t(pool);
        return execute(b, s, matmul::prepare, matmul::run, matmul::checksum), true;
    }
    if n == "Matmul::T8" {
        var pool = slice_pool<f64> {};
        var s = matmul::new_matmul8t(pool);
        return execute(b, s, matmul::prepare, matmul::run, matmul::checksum), true;
    }
    if n == "Matmul::T16" {
        var pool = slice_pool<f64> {};
        var s = matmul::new_matmul16t(pool);
        return execute(b, s, matmul::prepare, matmul::run, matmul::checksum), true;
    }
    if n == "Base64::Encode" {
        var s = base64_encode::new_base64_encode();
        return execute(b, s, base64_encode::prepare, base64_encode::run, base64_encode::checksum), true;
    }
    if n == "Base64::Decode" {
        var s = base64_decode::new_base64_decode();
        return execute(b, s, base64_decode::prepare, base64_decode::run, base64_decode::checksum), true;
    }
    if n == "Json::Generate" {
        var s = json_generate::new_json_generate();
        return execute(b, s, json_generate::prepare, json_generate::run, json_generate::checksum), true;
    }
    if n == "Json::ParseDom" {
        var s = json_parse_dom::new_json_parse_dom();
        return execute(b, s, json_parse_dom::prepare, json_parse_dom::run, json_parse_dom::checksum), true;
    }
    if n == "Json::ParseMapping" {
        var s = json_parse_mapping::new_json_parse_mapping();
        return execute(b, s, json_parse_mapping::prepare, json_parse_mapping::run,
                       json_parse_mapping::checksum), true;
    }
    if n == "CSV::Parse" {
        var s = csv_parse::new_csv_parse();
        return execute(b, s, csv_parse::prepare, csv_parse::run, csv_parse::checksum), true;
    }
    if n == "Etc::Sieve" { var s = sieve::new_sieve(); return execute(b, s, sieve::prepare, sieve::run, sieve::checksum), true; }
    if n == "Etc::TextRaytracer" {
        var s = text_raytracer::new_text_raytracer();
        return execute(b, s, text_raytracer::prepare, text_raytracer::run, text_raytracer::checksum), true;
    }
    if n == "Etc::NeuralNet" {
        var s = neural_net::new_neural_net();
        return execute(b, s, neural_net::prepare, neural_net::run, neural_net::checksum), true;
    }
    if n == "Etc::CacheSimulation" {
        // The slice pool of the cache's strings, which outlives the benchmark
        // whose cache keeps them (a struct can't hold a pool).
        reusable[] var strings: u8[>..] = [];
        var map = dictionary<u8[:], cache_simulation::Node?> {};
        var s = cache_simulation::new_cache_simulation(map);
        fn run(sim, iteration_id) { cache_simulation::run(sim, iteration_id, strings); }
        return execute(b, s, cache_simulation::prepare, run, cache_simulation::checksum), true;
    }
    if n == "Etc::GameOfLife" {
        var pool = slice_pool<game_of_life::Cell> {};
        var s = game_of_life::new_game_of_life(pool);
        return execute(b, s, game_of_life::prepare, game_of_life::run, game_of_life::checksum), true;
    }
    if n == "Etc::Words" { var s = words::new_words(); return execute(b, s, words::prepare, words::run, words::checksum), true; }
    if n == "Distance::Jaro" { var s = distance::new_jaro(); return execute(b, s, distance::prepare, distance::run, distance::checksum), true; }
    if n == "Distance::NGram" { var s = distance::new_ngram(); return execute(b, s, distance::prepare, distance::run, distance::checksum), true; }
    if n == "Etc::LogParser" {
        var s = log_parser::new_log_parser();
        return execute(b, s, log_parser::prepare, log_parser::run, log_parser::checksum), true;
    }
    if n == "Template::Regex" { return template::execute_regex(b), true; }
    if n == "Template::Parse" { return template::execute_parse(b), true; }
    if n == "Sort::Quick" { var s = sort::new_sort_quick(); return execute(b, s, sort::prepare, sort::run, sort::checksum), true; }
    if n == "Sort::Merge" { var s = sort::new_sort_merge(); return execute(b, s, sort::prepare, sort::run, sort::checksum), true; }
    if n == "Sort::Self" { var s = sort::new_sort_self(); return execute(b, s, sort::prepare, sort::run, sort::checksum), true; }
    if n == "Graph::BFS" || n == "Graph::DFS" || n == "Graph::AStar" {
        // The slice pool of the graph's adjacency lists, which outlives the
        // benchmark that keeps them; root owners cannot be nested in it.
        var edges = slice_pool<i32> {};
        fn prepare(s) { graph_path::prepare(s, edges); }
        if n == "Graph::BFS" { var s = graph_path::new_graph_path_bfs(); return execute(b, s, prepare, graph_path::run, graph_path::checksum), true; }
        if n == "Graph::DFS" { var s = graph_path::new_graph_path_dfs(); return execute(b, s, prepare, graph_path::run, graph_path::checksum), true; }
        if n == "Graph::AStar" { var s = graph_path::new_graph_path_astar(); return execute(b, s, prepare, graph_path::run, graph_path::checksum), true; }
    }
    if n == "Hash::SHA256" {
        var s = buffer_hash::new_buffer_hash_sha256();
        return execute(b, s, buffer_hash::prepare, buffer_hash::run, buffer_hash::checksum), true;
    }
    if n == "Hash::CRC32" {
        var s = buffer_hash::new_buffer_hash_crc32();
        return execute(b, s, buffer_hash::prepare, buffer_hash::run, buffer_hash::checksum), true;
    }
    if n == "Calculator::Ast" {
        // The program text and the node pool, which outlive the benchmark
        // that refers to them (a struct holds one growable array).
        var text: u8[>..] = [];
        var nodes: calculator_ast::Node..[>..] = [];
        var s = calculator_ast::new_calculator_ast(text, nodes);
        return execute(b, s, calculator_ast::prepare, calculator_ast::run, calculator_ast::checksum), true;
    }
    if n == "Calculator::Interpreter" {
        // The program text and the node pool of the interpreter's tree,
        // which outlive the benchmark that keeps the tree (a struct holds
        // one growable array).
        var text: u8[>..] = [];
        var nodes: calculator_ast::Node..[>..] = [];
        var s = calculator_interpreter::new_calculator_interpreter(text, nodes);
        return execute(b, s, calculator_interpreter::prepare, calculator_interpreter::run,
                       calculator_interpreter::checksum), true;
    }
    if n == "Maze::Generator" || n == "Maze::BFS" || n == "Maze::AStar" {
        // The slice pool of the maze's rows, which outlives the benchmark
        // that keeps them; root owners cannot be nested in it.
        var cells = slice_pool<maze::Cell> {};
        if n == "Maze::Generator" { var s = maze::new_maze_generator(cells); return execute(b, s, maze::prepare, maze::run, maze::checksum), true; }
        if n == "Maze::BFS" { var s = maze::new_maze_bfs(cells); return execute(b, s, maze::prepare, maze::run, maze::checksum), true; }
        if n == "Maze::AStar" { var s = maze::new_maze_astar(cells); return execute(b, s, maze::prepare, maze::run, maze::checksum), true; }
    }
    if n == "CLBG::Fannkuchredux" { var s = fannkuchredux::new_fannkuchredux(); return execute(b, s, fannkuchredux::prepare, fannkuchredux::run, fannkuchredux::checksum), true; }
    if n == "CLBG::Mandelbrot" { var s = mandelbrot::new_mandelbrot(); return execute(b, s, mandelbrot::prepare, mandelbrot::run, mandelbrot::checksum), true; }
    if n == "CLBG::Nbody" { var s = nbody::new_nbody(); return execute(b, s, nbody::prepare, nbody::run, nbody::checksum), true; }
    if n == "CLBG::Spectralnorm" {
        // The vector v, which outlives the benchmark that keeps it (a struct
        // holds one growable array).
        var v: f64[>..<] = [];
        var s = spectralnorm::new_spectralnorm(v);
        return execute(b, s, spectralnorm::prepare, spectralnorm::run, spectralnorm::checksum), true;
    }
    if starts_with(n, "Compress::") {
        // The test data, and a decoder's encoded result, which outlive the
        // benchmark that refers to them (see compress_data).
        var test_data: u8[>..<] = [];
        if n == "Compress::BWTEncode" { var s = bwt::new_bwt_encode(test_data); return execute(b, s, bwt::prepare, bwt::run, bwt::checksum), true; }
        if n == "Compress::BWTDecode" {
            var bwt_result = bwt::new_bwt_result();
            var s = bwt::new_bwt_decode(test_data, bwt_result);
            return execute(b, s, bwt::prepare, bwt::run, bwt::checksum), true;
        }
        if n == "Compress::HuffEncode" { var s = huffman::new_huff_encode(test_data); return execute(b, s, huffman::prepare, huffman::run, huffman::checksum), true; }
        if n == "Compress::HuffDecode" {
            var encoded = huffman::new_encoded_result();
            var s = huffman::new_huff_decode(test_data, encoded);
            return execute(b, s, huffman::prepare, huffman::run, huffman::checksum), true;
        }
        if n == "Compress::ArithEncode" { var s = arith::new_arith_encode(test_data); return execute(b, s, arith::prepare, arith::run, arith::checksum), true; }
        if n == "Compress::ArithDecode" {
            var encoded = arith::new_arith_encoded_result();
            var s = arith::new_arith_decode(test_data, encoded);
            return execute(b, s, arith::prepare, arith::run, arith::checksum), true;
        }
        if n == "Compress::LZWEncode" { var s = lzw::new_lzw_encode(test_data); return execute(b, s, lzw::prepare, lzw::run, lzw::checksum), true; }
        if n == "Compress::LZWDecode" {
            var encoded = lzw::new_lzw_result();
            var s = lzw::new_lzw_decode(test_data, encoded);
            return execute(b, s, lzw::prepare, lzw::run, lzw::checksum), true;
        }
    }
    return 0, false;
}

fn main() { run_main(dispatch); }
