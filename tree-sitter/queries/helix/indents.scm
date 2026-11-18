; Delimited bodies and lists; a closing delimiter cancels its indent.
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
  (array_literal)
  (const_for_constants)
] @indent

["}" ")" "]"] @outdent
