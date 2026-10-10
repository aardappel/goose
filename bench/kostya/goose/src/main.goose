// LangArena in Goose: runs the benchmarks a configuration file lists, in its
// order, and prints the per-benchmark time and checksum verdict in the format
// every other language's runner prints.
//
//   benchmark [config.js] [name-substring]

import std;
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
    if n == "Binarytrees::Obj" { return binarytrees::run_obj(b), true; }
    if n == "Binarytrees::Arena" { return binarytrees::run_arena(b), true; }
    if n == "Brainfuck::Array" { return brainfuck_array::run(b), true; }
    if n == "Brainfuck::Recursion" { return brainfuck_recursion::run(b), true; }
    if n == "Matmul::Single" { return matmul::single(b), true; }
    if n == "Matmul::T4" { return matmul::parallel(b, 4), true; }
    if n == "Matmul::T8" { return matmul::parallel(b, 8), true; }
    if n == "Matmul::T16" { return matmul::parallel(b, 16), true; }
    if n == "Base64::Encode" { return base64_encode::run(b), true; }
    if n == "Base64::Decode" { return base64_decode::run(b), true; }
    if n == "Json::Generate" { return json_generate::run(b), true; }
    if n == "Json::ParseDom" { return json_parse_dom::run(b), true; }
    if n == "Json::ParseMapping" { return json_parse_mapping::run(b), true; }
    if n == "CSV::Parse" { return csv_parse::run(b), true; }
    if n == "Etc::Sieve" { return sieve::run(b), true; }
    if n == "Etc::TextRaytracer" { return text_raytracer::run(b), true; }
    if n == "Etc::NeuralNet" { return neural_net::run(b), true; }
    if n == "Etc::CacheSimulation" { return cache_simulation::run(b), true; }
    if n == "Etc::GameOfLife" { return game_of_life::run(b), true; }
    if n == "Etc::Words" { return words::run(b), true; }
    if n == "Distance::Jaro" { return distance::run_jaro(b), true; }
    if n == "Distance::NGram" { return distance::run_ngram(b), true; }
    if n == "Etc::LogParser" { return log_parser::run(b), true; }
    if n == "Template::Regex" { return template::run_regex(b), true; }
    if n == "Template::Parse" { return template::run_parse(b), true; }
    if n == "Sort::Quick" { return sort::run_quick(b), true; }
    if n == "Sort::Merge" { return sort::run_merge(b), true; }
    if n == "Sort::Self" { return sort::run_self(b), true; }
    if n == "Graph::BFS" { return graph_path::bfs(b), true; }
    if n == "Graph::DFS" { return graph_path::dfs(b), true; }
    if n == "Graph::AStar" { return graph_path::astar(b), true; }
    if n == "Hash::SHA256" { return buffer_hash::sha256(b), true; }
    if n == "Hash::CRC32" { return buffer_hash::crc32(b), true; }
    if n == "Calculator::Ast" { return calculator_ast::run(b), true; }
    if n == "Calculator::Interpreter" { return calculator_interpreter::run(b), true; }
    if n == "Maze::Generator" { return maze::generator(b), true; }
    if n == "Maze::BFS" { return maze::bfs_bench(b), true; }
    if n == "Maze::AStar" { return maze::astar_bench(b), true; }
    if n == "CLBG::Fannkuchredux" { return fannkuchredux::run(b), true; }
    if n == "CLBG::Mandelbrot" { return mandelbrot::run(b), true; }
    if n == "CLBG::Nbody" { return nbody::run(b), true; }
    if n == "CLBG::Spectralnorm" { return spectralnorm::run(b), true; }
    if n == "Compress::BWTEncode" { return bwt::run_encode(b), true; }
    if n == "Compress::BWTDecode" { return bwt::run_decode(b), true; }
    if n == "Compress::HuffEncode" { return huffman::run_encode(b), true; }
    if n == "Compress::HuffDecode" { return huffman::run_decode(b), true; }
    if n == "Compress::ArithEncode" { return arith::run_encode(b), true; }
    if n == "Compress::ArithDecode" { return arith::run_decode(b), true; }
    if n == "Compress::LZWEncode" { return lzw::run_encode(b), true; }
    if n == "Compress::LZWDecode" { return lzw::run_decode(b), true; }
    return 0, false;
}

fn main() { run_main(dispatch); }
