/**
 * @file Goose grammar for tree-sitter
 * @license Apache-2.0
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// Tree-sitter grammar for Goose (https://github.com/aardappel/goose), after
// the compiler's recursive-descent parser (src/parser.h in that repository)
// and the spec's grammar sketch (docs/goose_spec.md, Appendix D).
//
// The grammar is laxer than the compiler where laxness keeps it small
// (declaration modifiers in any order, any expression as an assignment
// target); it never rejects a program the compiler accepts. Control
// constructs appear only where the compiler parses a full expression, never
// as an operand, so a statement that is one ends at its `}`. Struct literals
// and trailing blocks are not excluded from scrutinee position: the parse
// that reads `if x {` as a struct literal fails at the missing body.

const PREC = {
  OR: 1,
  AND: 2,
  EQUALITY: 3,
  COMPARE: 4,
  BIT_OR: 5,
  BIT_XOR: 6,
  BIT_AND: 7,
  SHIFT: 8,
  ADDITIVE: 9,
  MULTIPLICATIVE: 10,
  UNARY: 11,
  POSTFIX: 12,
};

const PRIMITIVE_TYPES = [
  'bool', 'varint',
  'i8', 'i16', 'i32', 'i64',
  'u8', 'u16', 'u32', 'u64',
  'f32', 'f64',
];

const UNSIGNED_TYPES = ['u8', 'u16', 'u32', 'u64'];

const ASSIGNMENT_OPERATORS = [
  '=', '.=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>=',
];

function sep1(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)));
}

function commaSep(rule) {
  return optional(seq(sep1(rule, ','), optional(',')));
}

function commaSep1(rule) {
  return seq(sep1(rule, ','), optional(','));
}

module.exports = grammar({
  name: 'goose',

  word: $ => $.identifier,

  extras: $ => [/\s/, /﻿/, $.line_comment, $.block_comment],

  // Block comments nest, which a regular token cannot match.
  externals: $ => [$.block_comment],

  supertypes: $ => [$._expression, $._type],

  conflicts: $ => [
    // `{ a, b => … }` against `{ a, b = f(); }`, and `{ a => … }` against a
    // block whose value is `a`.
    [$._expression_name, $.function_value_parameter],
    [$._qualified_name, $.function_value_parameter],
    // `Name.Variant { … }` against a field access, and `f<T>(x)` or
    // `Name<T> { … }` against a comparison: what follows decides.
    [$._primary_expression, $.struct_literal],
    [$._primary_expression, $._callee, $.struct_literal],
    // `if f(x) {` against a trailing block.
    [$.call_expression, $._trailing_call],
  ],

  rules: {
    source_file: $ => repeat($._top_level_item),

    _top_level_item: $ => choice(
      $.namespace_declaration,
      $.import_declaration,
      $.struct_declaration,
      $.enum_declaration,
      $.type_alias_declaration,
      $.function_declaration,
      $.extern_block,
      $.variable_declaration,
    ),

    namespace_declaration: $ => seq('namespace', field('name', $.identifier), ';'),

    // `import a.b;`, `import .a.b;` (relative), `import name from "source";`.
    import_declaration: $ => seq(
      'import',
      optional('.'),
      field('path', $.import_path),
      optional(seq('from', field('source', $.string_literal))),
      ';',
    ),

    import_path: $ => sep1($.identifier, '.'),

    // `name`, `ns::name`, or `::name`.
    scoped_identifier: $ => seq(
      optional(field('namespace', $.identifier)),
      '::',
      field('name', $.identifier),
    ),

    _qualified_name: $ => choice($.identifier, $.scoped_identifier),

    // --- Declarations ---

    struct_declaration: $ => seq(
      'struct',
      field('name', $._qualified_name),
      optional(field('type_parameters', $.type_parameters)),
      field('body', $.field_declaration_list),
    ),

    field_declaration_list: $ => seq(
      '{',
      commaSep(choice($.field_declaration, $.pad_declaration)),
      '}',
    ),

    field_declaration: $ => seq(
      optional(choice('let', 'const')),
      field('name', $.identifier),
      ':',
      field('type', $._type),
      optional(seq('=', field('default', $._expression))),
    ),

    pad_declaration: $ => seq('pad', optional(field('size', $.int_literal))),

    enum_declaration: $ => seq(
      'enum',
      field('name', $._qualified_name),
      optional(field('type_parameters', $.type_parameters)),
      '{',
      commaSep($.enum_variant),
      '}',
    ),

    enum_variant: $ => seq(
      field('name', $.identifier),
      optional(field('body', $.field_declaration_list)),
    ),

    type_alias_declaration: $ => seq(
      'type',
      field('name', $._qualified_name),
      '=',
      field('type', $._type),
      ';',
    ),

    type_parameters: $ => seq('<', commaSep1($.type_parameter), '>'),

    type_parameter: $ => seq(
      field('name', $.identifier),
      optional(seq(':', field('bound', $._type))),
    ),

    function_declaration: $ => seq(
      repeat($.function_modifier),
      choice('fn', 'thread_fn'),
      field('name', $._qualified_name),
      optional(field('type_parameters', $.type_parameters)),
      field('parameters', $.parameters),
      optional(seq('->', field('result', sep1($._type, ',')))),
      // An extern's C symbol goes where its body would.
      choice(field('body', $.block), seq(optional($._symbol), ';')),
    ),

    // `extern "lib" { ... }`: functions bound from a shared library, each
    // written without `extern`, with `= "sym"` for its symbol.
    extern_block: $ => seq(
      'extern',
      field('library', $.string_literal),
      '{',
      repeat($.extern_member),
      '}',
    ),

    extern_member: $ => seq(
      'fn',
      field('name', $._qualified_name),
      optional(field('type_parameters', $.type_parameters)),
      field('parameters', $.parameters),
      optional(seq('->', field('result', sep1($._type, ',')))),
      optional($._symbol),
      ';',
    ),

    // `= "sym"` after an extern's signature: the C symbol it binds.
    _symbol: $ => seq('=', field('symbol', $.string_literal)),

    // `export` takes the C symbol's name, as `extern` did before the
    // symbol moved after the signature (still accepted, deprecated).
    function_modifier: $ => choice(
      'checkfree',
      'simd',
      'inline',
      'recursive',
      seq('export', optional($.string_literal)),
      seq('extern', optional($.string_literal)),
    ),

    parameters: $ => seq('(', commaSep($.parameter), ')'),

    parameter: $ => seq(
      optional('var'),
      field('name', $.identifier),
      optional(seq(':', field('type', $._type))),
      optional(seq('=', field('default', $._expression))),
    ),

    // Globals and locals; `.=` binds a reference.
    variable_declaration: $ => seq(
      optional(seq('reusable', optional(seq('[', ']')))),
      choice('let', 'var', 'const'),
      sep1(field('name', $._qualified_name), ','),
      optional(seq(':', field('type', $._type))),
      optional(seq(choice('=', '.='), sep1(field('value', $._expression), ','))),
      ';',
    ),

    // --- Types ---

    _type: $ => choice($._type_no_index, $.index_type),

    // A for binder's type, which its loop's own `in` follows: an index type
    // there needs parentheses.
    _type_no_index: $ => choice(
      $._postfix_type,
      $.const_type,
    ),

    // `const` qualifies the first reference or slice built on the base type.
    const_type: $ => seq('const', $._postfix_type),

    _postfix_type: $ => choice(
      $.primitive_type,
      $.named_type,
      $.function_type,
      $.parenthesized_type,
      $.array_type,
      $.slice_type,
      $.reference_type,
      $.optional_type,
      $.variable_mode_type,
      $.variant_type,
    ),

    // Ends before an `in`, which in a for binder's type is the loop's.
    primitive_type: _ => prec(1, choice(...PRIMITIVE_TYPES)),

    named_type: $ => prec.right(seq(
      field('name', $._qualified_name),
      optional(field('arguments', $.type_arguments)),
    )),

    type_arguments: $ => seq('<', commaSep1($._type), '>'),

    function_type: $ => prec.right(1, seq(
      'fn',
      optional(seq(
        '(',
        commaSep($._type),
        ')',
        optional(seq('->', choice(
          field('result', $._type),
          seq('(', commaSep(field('result', $._type)), ')'),
        ))),
      )),
    )),

    parenthesized_type: $ => seq('(', $._type, ')'),

    // `T[k]`, `T[]`, `T[u16]`, `T[..k]`, `T[>..]`, `T[>..<]`.
    array_type: $ => prec(1, seq(
      field('element', $._postfix_type),
      '[',
      optional(choice(
        field('size', $._expression),
        field('length', alias(choice(...UNSIGNED_TYPES, 'varint'), $.primitive_type)),
        seq('..', optional(field('capacity', $._expression))),
        seq('>', '..', optional('<')),
      )),
      ']',
    )),

    slice_type: $ => prec(1, seq(field('element', $._postfix_type), '[', ':', ']')),

    // `T&`, and the relative `T&<u8>` / `T&<u32 in pool>`.
    reference_type: $ => prec.right(1, seq(
      field('target', $._postfix_type),
      '&',
      optional(seq(
        '<',
        field('width', alias(choice(...UNSIGNED_TYPES, 'varint'), $.primitive_type)),
        optional(seq('in', field('pool', $._qualified_name))),
        '>',
      )),
    )),

    optional_type: $ => prec(1, seq(field('target', $._postfix_type), '?')),

    variable_mode_type: $ => prec(1, seq(field('target', $._postfix_type), '..')),

    variant_type: $ => prec(1, seq(
      field('target', $._postfix_type),
      '.',
      field('variant', $.identifier),
    )),

    // `u32 in nodes` / `u32 upto nodes`: takes no postfix.
    index_type: $ => seq(
      optional('const'),
      alias(choice(...UNSIGNED_TYPES), $.primitive_type),
      choice('in', 'upto'),
      field('domain', $._qualified_name),
    ),

    // --- Statements ---

    block: $ => seq('{', optional($._block_body), '}'),

    _block_body: $ => choice(
      seq(repeat1($._statement), optional($._tail_expression)),
      $._tail_expression,
    ),

    // A block's value; the block-ended constructs stand as statements there.
    _tail_expression: $ => choice($._value_expression, $._jump_expression),

    _statement: $ => choice(
      $.variable_declaration,
      $.function_declaration,
      $.const_for_statement,
      $.guard_statement,
      $.assignment_statement,
      $.increment_statement,
      $.expression_statement,
      $._block_ended_expression,
      prec(1, alias($._trailing_call, $.call_expression)),
      $.empty_statement,
    ),

    expression_statement: $ => seq(choice($._value_expression, $._jump_expression), ';'),

    empty_statement: _ => ';',

    // `a, b = f();` takes a call's results.
    assignment_statement: $ => seq(
      sep1(field('left', $._value_expression), ','),
      field('operator', choice(...ASSIGNMENT_OPERATORS)),
      field('right', $._expression),
      ';',
    ),

    increment_statement: $ => seq(
      field('operand', $._value_expression),
      field('operator', choice('++', '--')),
      ';',
    ),

    // Guards the rest of the block it is in.
    guard_statement: $ => seq(
      'guard',
      field('condition', $._expression),
      choice(';', seq('else', field('alternative', $.block))),
    ),

    // Compile-time expansion over fields, variants, types, or constants.
    const_for_statement: $ => seq($._const_for_head, field('body', $.block)),

    _const_for_head: $ => seq(
      'const',
      'for',
      '<',
      commaSep1(field('name', $.identifier)),
      '>',
      'in',
      field('source', choice($.const_for_types, $.const_for_constants)),
    ),

    const_for_types: $ => seq(
      choice('fields', 'variants', 'types'),
      '<',
      commaSep($._type),
      '>',
    ),

    const_for_constants: $ => seq('[', commaSep($._expression), ']'),

    // --- Expressions ---

    _expression: $ => choice(
      $._value_expression,
      $._block_ended_expression,
      $._jump_expression,
    ),

    _block_ended_expression: $ => choice(
      $.if_expression,
      $.match_expression,
      $.block_expression,
      $.block,
      $.while_expression,
      $.loop_expression,
      $.for_expression,
    ),

    _jump_expression: $ => choice(
      $.return_expression,
      $.break_expression,
      $.continue_expression,
    ),

    // What an operand may be: no control construct without parentheses.
    _value_expression: $ => choice(
      $.binary_expression,
      $.unary_expression,
      $._postfix_expression,
    ),

    _postfix_expression: $ => choice(
      $._primary_expression,
      $.call_expression,
      alias($._trailing_call, $.call_expression),
      $.index_expression,
      $.slice_expression,
      $.field_expression,
      $.cast_expression,
    ),

    _primary_expression: $ => choice(
      $._expression_name,
      $.int_literal,
      $.float_literal,
      $.char_literal,
      $.string_literal,
      $.raw_string_literal,
      $.boolean_literal,
      $.null,
      $.self,
      $.parenthesized_expression,
      $.array_literal,
      $.struct_literal,
    ),

    _expression_name: $ => choice($.identifier, $.scoped_identifier),

    binary_expression: $ => {
      const table = [
        ['||', PREC.OR],
        ['&&', PREC.AND],
        ['==', PREC.EQUALITY],
        ['!=', PREC.EQUALITY],
        ['.==', PREC.EQUALITY],
        ['.!=', PREC.EQUALITY],
        ['<', PREC.COMPARE],
        ['<=', PREC.COMPARE],
        ['>', PREC.COMPARE],
        ['>=', PREC.COMPARE],
        ['|', PREC.BIT_OR],
        ['^', PREC.BIT_XOR],
        ['&', PREC.BIT_AND],
        ['<<', PREC.SHIFT],
        ['>>', PREC.SHIFT],
        ['+', PREC.ADDITIVE],
        ['-', PREC.ADDITIVE],
        ['*', PREC.MULTIPLICATIVE],
        ['/', PREC.MULTIPLICATIVE],
        ['%', PREC.MULTIPLICATIVE],
      ];
      return choice(...table.map(([operator, precedence]) =>
        prec.left(precedence, seq(
          field('left', $._value_expression),
          field('operator', operator),
          field('right', $._value_expression),
        ))));
    },

    unary_expression: $ => prec(PREC.UNARY, seq(
      field('operator', choice('-', '!', '~', '&')),
      field('operand', $._value_expression),
    )),

    // `as!` converts unchecked.
    cast_expression: $ => prec(PREC.POSTFIX, seq(
      field('value', $._postfix_expression),
      'as',
      optional('!'),
      field('type', $._type),
    )),

    call_expression: $ => prec(PREC.POSTFIX, seq(
      $._callee,
      field('arguments', $.argument_list),
    )),

    // A call with a trailing function value; on a statement's spine it ends
    // the statement.
    _trailing_call: $ => prec(PREC.POSTFIX, seq(
      $._callee,
      field('arguments', $.argument_list),
      field('trailing', $.function_value),
    )),

    _callee: $ => choice(
      field('function', $._postfix_expression),
      seq(field('function', $._expression_name), field('type_arguments', $.type_arguments)),
    ),

    argument_list: $ => seq('(', commaSep($._expression), ')'),

    // `{ x => … }`, `{ a, b: T => … }`, or `{ … }` with an implicit `it`.
    function_value: $ => seq(
      '{',
      optional(seq(field('parameters', $.function_value_parameters), '=>')),
      optional($._block_body),
      '}',
    ),

    function_value_parameters: $ => sep1($.function_value_parameter, ','),

    function_value_parameter: $ => seq(
      optional('var'),
      field('name', $.identifier),
      optional(seq(':', field('type', $._type))),
    ),

    index_expression: $ => prec(PREC.POSTFIX, seq(
      field('value', $._postfix_expression),
      '[',
      field('index', $._expression),
      ']',
    )),

    // `a[lo..hi]`, either bound optional; `^k` counts from the end.
    slice_expression: $ => prec(PREC.POSTFIX, seq(
      field('value', $._postfix_expression),
      '[',
      optional(field('start', $.slice_bound)),
      '..',
      optional(field('end', $.slice_bound)),
      ']',
    )),

    slice_bound: $ => seq(optional('^'), $._expression),

    field_expression: $ => prec(PREC.POSTFIX, seq(
      field('value', $._postfix_expression),
      '.',
      field('field', $.identifier),
    )),

    parenthesized_expression: $ => seq('(', $._expression, ')'),

    // `[a, b]`, `[v; n]`, or `[..capacity]`.
    array_literal: $ => seq(
      '[',
      optional(choice(
        commaSep1($._expression),
        seq(field('value', $._expression), ';', field('count', $._expression)),
        seq('..', field('capacity', $._expression)),
      )),
      ']',
    ),

    // `Name { … }`, `Name<T> { … }`, `Name.Variant { … }`, `Name<T>.Variant { … }`.
    struct_literal: $ => seq(
      field('name', $._expression_name),
      optional(field('type_arguments', $.type_arguments)),
      optional(seq('.', field('variant', $.identifier))),
      field('body', $.initializer_list),
    ),

    // A trailing `..` fills in every field not given.
    initializer_list: $ => seq(
      '{',
      repeat(seq($._initializer, ',')),
      optional(choice($._initializer, '..')),
      '}',
    ),

    _initializer: $ => choice(
      $.field_initializer,
      $._expression,
      $.const_for_initializer,
    ),

    field_initializer: $ => seq(field('name', $.identifier), ':', field('value', $._expression)),

    const_for_initializer: $ => seq($._const_for_head, field('body', $.initializer_list)),

    if_expression: $ => prec.right(seq(
      'if',
      field('condition', $._expression),
      field('consequence', $.block),
      optional(seq('else', field('alternative', choice($.block, $.if_expression)))),
    )),

    match_expression: $ => seq(
      'match',
      field('value', $._expression),
      '{',
      commaSep($._match_arm),
      '}',
    ),

    _match_arm: $ => choice($.match_arm, $.const_for_arms),

    // Arms expanded at specialization time: `const for <N, V> in variants<E> { V v => ..., }`.
    const_for_arms: $ => seq(
      $._const_for_head,
      '{',
      commaSep($._match_arm),
      '}',
    ),

    match_arm: $ => seq(
      field('pattern', $._pattern),
      '=>',
      field('value', $._expression),
    ),

    _pattern: $ => choice(
      $.wildcard_pattern,
      $.variant_pattern,
      $.pattern_list,
    ),

    wildcard_pattern: _ => '_',

    // A lone variant binding its payload: `Circle c`, `Circle &c`.
    variant_pattern: $ => seq(
      field('variant', $.identifier),
      optional('&'),
      field('binding', $.identifier),
    ),

    // Variants, constants, literals, and ranges: `A, B`, `1..9, -1`.
    pattern_list: $ => sep1(choice($._pattern_bound, $.range_pattern), ','),

    range_pattern: $ => seq(
      field('start', $._pattern_bound),
      '..',
      field('end', $._pattern_bound),
    ),

    _pattern_bound: $ => choice(
      $._expression_name,
      $.int_literal,
      $.char_literal,
      $.negative_pattern,
    ),

    negative_pattern: $ => seq('-', choice($._expression_name, $.int_literal, $.char_literal)),

    block_expression: $ => seq('block', field('body', $.block)),

    while_expression: $ => seq(
      'while',
      field('condition', $._expression),
      field('body', $.block),
    ),

    loop_expression: $ => seq('loop', field('body', $.block)),

    for_expression: $ => seq(
      'for',
      optional('&'),
      field('name', $.identifier),
      optional(seq(':', field('type', $._type_no_index))),
      optional(seq(
        ',',
        field('index', $.identifier),
        optional(seq(':', field('index_type', $._type_no_index))),
      )),
      'in',
      field('iterable', choice($._expression, $.range_expression)),
      field('body', $.block),
    ),

    range_expression: $ => seq(
      field('start', $._expression),
      '..',
      field('end', $._expression),
    ),

    // `return a, b from f` returns from the enclosing function f.
    return_expression: $ => prec.right(seq(
      'return',
      optional(sep1($._expression, ',')),
      optional(seq('from', field('target', $._qualified_name))),
    )),

    break_expression: $ => prec.right(seq('break', optional($._expression))),

    continue_expression: _ => 'continue',

    // --- Terminals ---

    identifier: _ => /[A-Za-z_][A-Za-z0-9_]*/,

    int_literal: _ => token(choice(
      /[0-9]+/,
      /0[xX][0-9a-fA-F]+/,
    )),

    // A `.` starts a fraction only when a digit follows, so `1..2` is a range.
    float_literal: _ => token(choice(
      /[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?/,
      /[0-9]+[eE][+-]?[0-9]+/,
      /0[xX][0-9a-fA-F]+(\.[0-9a-fA-F]+)?[pP][+-]?[0-9]+/,
    )),

    // An integer constant.
    char_literal: _ => token(seq(
      '\'',
      choice(/[^'\\\r\n]/, /\\[ntr0\\"']/, /\\x[0-9a-fA-F]{2}/),
      '\'',
    )),

    string_literal: $ => seq(
      '"',
      repeat(choice(
        alias(token.immediate(prec(1, /[^"\\\r\n]+/)), $.string_content),
        $.escape_sequence,
      )),
      token.immediate('"'),
    ),

    escape_sequence: _ => token.immediate(seq(
      '\\',
      choice(/[ntr0\\"']/, /x[0-9a-fA-F]{2}/),
    )),

    // No escapes; the next `"""` ends it, on one line or across several.
    raw_string_literal: _ => token(seq(
      '"""',
      repeat(choice(/[^"]/, /"[^"]/, /""[^"]/)),
      '"""',
    )),

    boolean_literal: _ => choice('true', 'false'),

    null: _ => 'null',

    self: _ => 'self',

    line_comment: _ => token(seq('//', /[^\n]*/)),
  },
});
