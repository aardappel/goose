; Helix capture names. Broad rules precede the more specific patterns.
(identifier) @variable

((identifier) @variable.builtin
 (#eq? @variable.builtin "it"))
(parameter name: (identifier) @variable.parameter)
(function_value_parameter name: (identifier) @variable.parameter)

(field_declaration name: (identifier) @variable.other.member)
(field_expression field: (identifier) @variable.other.member)
(field_initializer name: (identifier) @variable.other.member)
(storage_name field: (identifier) @variable.other.member)

(namespace_declaration name: (identifier) @namespace)
(import_path (identifier) @namespace)
(scoped_identifier namespace: (identifier) @namespace)

(function_declaration name: (identifier) @function)
(function_declaration name: (scoped_identifier name: (identifier) @function))
(extern_member name: (identifier) @function)
(extern_member name: (scoped_identifier name: (identifier) @function))
(call_expression function: (identifier) @function)
(call_expression function: (scoped_identifier name: (identifier) @function))
(call_expression function: (field_expression field: (identifier) @function.method))
(return_expression target: (identifier) @function)
(return_expression target: (scoped_identifier name: (identifier) @function))

(struct_declaration name: (identifier) @type)
(struct_declaration name: (scoped_identifier name: (identifier) @type))
(enum_declaration name: (identifier) @type)
(enum_declaration name: (scoped_identifier name: (identifier) @type))
(type_alias_declaration name: (identifier) @type)
(type_alias_declaration name: (scoped_identifier name: (identifier) @type))
(type_parameter name: (identifier) @type.parameter)
(named_type name: (identifier) @type)
(named_type name: (scoped_identifier name: (identifier) @type))
(struct_literal name: (identifier) @type)
(struct_literal name: (scoped_identifier name: (identifier) @type))
(variant_constant name: (identifier) @type)
(variant_constant name: (scoped_identifier name: (identifier) @type))
(primitive_type) @type.builtin

(enum_variant name: (identifier) @type.enum.variant)
(variant_type variant: (identifier) @type.enum.variant)
(struct_literal variant: (identifier) @type.enum.variant)
(variant_constant variant: (identifier) @type.enum.variant)
(variant_pattern variant: (identifier) @type.enum.variant)
(pattern_list (identifier) @constant)
(range_pattern (identifier) @constant)
(negative_pattern (identifier) @constant)

; Payload-less variants without type arguments are parsed as field reads.
((field_expression value: (identifier) @type field: (identifier) @type.enum.variant)
 (#match? @type "^[A-Z]")
 (#match? @type.enum.variant "^[A-Z]"))

(wildcard_pattern) @variable.builtin
(self) @variable.builtin
(function_modifier) @keyword.storage.modifier
"root" @keyword.storage.modifier
(const_query name: (identifier) @keyword)

["fn" "thread_fn"] @keyword.function
["struct" "enum" "type"] @keyword.storage.type
["let" "var" "const" "reusable"] @keyword.storage
["pad" "namespace" "block" "fields" "variants" "types" "in" "upto"] @keyword
"as" @keyword.operator
["import" "from"] @keyword.control.import
["return" "break" (continue_expression)] @keyword.control.return
["if" "else" "match" "guard"] @keyword.control.conditional
["while" "for" "loop"] @keyword.control.repeat
"defer" @keyword.control
(extern_block "extern" @keyword.storage.modifier)

(boolean_literal) @constant.builtin.boolean
(null) @constant.builtin
(int_literal) @constant.numeric.integer
(float_literal) @constant.numeric.float
(char_literal) @constant.character
(string_literal) @string
(raw_string_literal) @string
(escape_sequence) @constant.character.escape

[
  "=" ".="
  "+" "-" "*" "/" "%"
  "==" "!=" ".==" ".!=" "<" "<=" ">" ">="
  "&&" "||" "!"
  "&" "|" "^" "~" "<<" ">>"
  "+=" "-=" "*=" "/=" "%="
  "&=" "|=" "^=" "<<=" ">>="
  "++" "--"
  "->" "=>"
  ".." "?"
] @operator

["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["," ";" ":" "::" "."] @punctuation.delimiter
(line_comment) @comment.line
(block_comment) @comment.block
