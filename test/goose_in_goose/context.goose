// One compilation owns each table locally. The fixed-size context borrows
// those tables so recursive passes can share them without global state.
struct CValue {
    code: const u8[:],
    ty: i64,
    address: const u8[:] = "",
    stack: const u8[:] = "",
}
struct Control { again: i64, done: i64 }
struct FunctionInstance { template: i64, arguments: i64, symbol: i64 }

struct Compiler {
    function_instances: FunctionInstance[>..]&,
    checked_calls: CheckedCall[>..]&,
    shrinks: Shrink[>..]&,
    effect_revision: i64 = 0,
    visit: i64 = 0,
    specializations: Specialization[>..]&,
    checking_function: i64 = -1,
    loop_depth: i64 = 0,
    global_order: i64[>..]&,
    c_output: u8[>..]&,
    c_declarations: u8[>..]&,
    c_temporary: i64 = 0,
    c_function: i64 = -1,
    c_slots: i64 = 0,
    c_global_slots: i64 = 0,
    symbol_slots: i64[>..]&,
    c_type_state: i64[>..]&,
    function_slots: i64[>..]&,
    return_locals: i64[>..]&,
    c_final: u8[>..]&,
    c_controls: Control[>..<]&,
    c_arguments: (const u8[:])[>..<]&,
    c_label: i64 = 0,
    c_result: CValue = CValue { code: "", ty: 0 },
    text_storage: u8[>..]&,
    sources: Source[>..]&,
    tokens: Token[>..]&,
    nodes: Node[>..]&,
    edges: Edge[>..]&,
    declarations: i64[>..]&,
    current_namespace: i64 = -1,
    cursor: i64 = 0,
    parse_limit: i64 = 0,
    parse_depth: i64 = 0,
    types: Type[>..]&,
    fielddefs: Field[>..]&,
    scopes: Scope[>..]&,
    symbols: Symbol[>..]&,
}

fn run_compilation(mode: i64, path: const u8[:], output: const u8[:]) {
    var owned_function_instances: FunctionInstance[>..] = [];
    var owned_checked_calls: CheckedCall[>..] = [];
    var owned_shrinks: Shrink[>..] = [];
    var owned_specializations: Specialization[>..] = [];
    var owned_global_order: i64[>..] = [];
    var owned_c_output: u8[>..] = [];
    var owned_c_declarations: u8[>..] = [];
    var owned_symbol_slots: i64[>..] = [];
    var owned_c_type_state: i64[>..] = [];
    var owned_function_slots: i64[>..] = [];
    var owned_return_locals: i64[>..] = [];
    var owned_c_final: u8[>..] = [];
    var owned_c_controls: Control[>..<] = [];
    var owned_c_arguments: (const u8[:])[>..<] = [];
    var owned_text_storage: u8[>..] = [];
    var owned_sources: Source[>..] = [];
    var owned_tokens: Token[>..] = [];
    var owned_nodes: Node[>..] = [];
    var owned_edges: Edge[>..] = [];
    var owned_declarations: i64[>..] = [];
    var owned_types: Type[>..] = [];
    var owned_fielddefs: Field[>..] = [];
    var owned_scopes: Scope[>..] = [];
    var owned_symbols: Symbol[>..] = [];
    var ctx = Compiler {
        function_instances: owned_function_instances,
        checked_calls: owned_checked_calls,
        shrinks: owned_shrinks,
        specializations: owned_specializations,
        global_order: owned_global_order,
        c_output: owned_c_output,
        c_declarations: owned_c_declarations,
        symbol_slots: owned_symbol_slots,
        c_type_state: owned_c_type_state,
        function_slots: owned_function_slots,
        return_locals: owned_return_locals,
        c_final: owned_c_final,
        c_controls: owned_c_controls,
        c_arguments: owned_c_arguments,
        text_storage: owned_text_storage,
        sources: owned_sources,
        tokens: owned_tokens,
        nodes: owned_nodes,
        edges: owned_edges,
        declarations: owned_declarations,
        types: owned_types,
        fielddefs: owned_fielddefs,
        scopes: owned_scopes,
        symbols: owned_symbols,
    };
    compile_command(ctx, mode, path, output);
}
