; Goose highlighting, nvim-style capture names. Later patterns win where two
; cover the same bytes, so the broad rules come first.

(identifier) @variable

; The implicit parameter of a function value without a parameter list.
((identifier) @variable.builtin
 (#eq? @variable.builtin "it"))

(parameter name: (identifier) @variable.parameter)
(function_value_parameter name: (identifier) @variable.parameter)

(field_declaration name: (identifier) @property)
(field_expression field: (identifier) @property)
(field_initializer name: (identifier) @property)

(namespace_declaration name: (identifier) @module)
(import_path (identifier) @module)
(scoped_identifier namespace: (identifier) @module)

(function_declaration name: (identifier) @function)
(function_declaration name: (scoped_identifier name: (identifier) @function))
(call_expression function: (identifier) @function.call)
(call_expression function: (scoped_identifier name: (identifier) @function.call))
; A call through a dot is a method call or a call of a free function by UFCS.
(call_expression function: (field_expression field: (identifier) @function.method.call))
(return_expression target: (identifier) @function)
(return_expression target: (scoped_identifier name: (identifier) @function))

(struct_declaration name: (identifier) @type)
(struct_declaration name: (scoped_identifier name: (identifier) @type))
(enum_declaration name: (identifier) @type)
(enum_declaration name: (scoped_identifier name: (identifier) @type))
(type_alias_declaration name: (identifier) @type)
(type_alias_declaration name: (scoped_identifier name: (identifier) @type))
(type_parameter name: (identifier) @type)
(named_type name: (identifier) @type)
(named_type name: (scoped_identifier name: (identifier) @type))
(struct_literal name: (identifier) @type)
(struct_literal name: (scoped_identifier name: (identifier) @type))
(primitive_type) @type.builtin

(enum_variant name: (identifier) @constant)
(variant_type variant: (identifier) @constant)
(struct_literal variant: (identifier) @constant)
(variant_pattern variant: (identifier) @constant)
(pattern_list (identifier) @constant)
(range_pattern (identifier) @constant)
(negative_pattern (identifier) @constant)

; A payload-less variant: Shape.Dot. Types and variants are PascalCase by
; convention, which separates them from a field read.
((field_expression value: (identifier) @type field: (identifier) @constant)
 (#match? @type "^[A-Z]")
 (#match? @constant "^[A-Z]"))

(wildcard_pattern) @variable.builtin
(self) @variable.builtin

(function_modifier) @keyword.modifier

[
  "fn"
  "thread_fn"
  "struct"
  "enum"
  "type"
  "let"
  "var"
  "const"
  "pad"
  "reusable"
  "namespace"
  "block"
  "fields"
  "variants"
  "types"
  "in"
  "upto"
] @keyword

"as" @keyword.operator

[
  "import"
  "from"
] @keyword.import

"return" @keyword.return

[
  "if"
  "else"
  "match"
  "guard"
] @keyword.conditional

[
  "while"
  "for"
  "loop"
  "break"
  (continue_expression)
] @keyword.repeat

(boolean_literal) @boolean
(null) @constant.builtin
(int_literal) @number
(float_literal) @number.float
(char_literal) @character
(string_literal) @string
(raw_string_literal) @string
(escape_sequence) @escape

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
  ".."
  "?"
] @operator

[
  "(" ")" "[" "]" "{" "}"
] @punctuation.bracket

[
  "," ";" ":" "::" "."
] @punctuation.delimiter

(line_comment) @comment
(block_comment) @comment
