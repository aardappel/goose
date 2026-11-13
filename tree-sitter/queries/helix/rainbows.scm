; Scopes are direct parents of paired delimiters.
[
  (block)
  (field_declaration_list)
  (enum_declaration)
  (extern_block)
  (match_expression)
  (const_for_arms)
  (initializer_list)
  (function_value)
  (parameters)
  (argument_list)
  (parenthesized_expression)
  (parenthesized_type)
  (function_type)
  (array_type)
  (slice_type)
  (array_literal)
  (index_expression)
  (slice_expression)
  (const_for_constants)
  (type_parameters)
  (type_arguments)
  (const_for_types)
  (const_query)
  (const_for_statement)
  (const_for_initializer)
] @rainbow.scope

["(" ")" "[" "]" "{" "}"] @rainbow.bracket

; Angle brackets are also operators. Match them only in delimited contexts.
(reference_type "<" ">") @rainbow.scope
(type_parameters ["<" ">"] @rainbow.bracket)
(type_arguments ["<" ">"] @rainbow.bracket)
(const_for_types ["<" ">"] @rainbow.bracket)
(const_query ["<" ">"] @rainbow.bracket)
(reference_type ["<" ">"] @rainbow.bracket)
(const_for_statement ["<" ">"] @rainbow.bracket)
(const_for_initializer ["<" ">"] @rainbow.bracket)
(const_for_arms ["<" ">"] @rainbow.bracket)
