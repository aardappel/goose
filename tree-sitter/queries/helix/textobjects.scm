; Function bodies include their braces, as in Helix's Go queries.
(function_declaration
  body: (block)? @function.inside) @function.around
(extern_member) @function.around
(function_value) @function.inside @function.around

(struct_declaration
  body: (field_declaration_list) @class.inside) @class.around

; Helix combines the two captures into a range including both braces.
(enum_declaration) @class.around
(enum_declaration "{" @class.inside "}" @class.inside)
(type_alias_declaration) @class.around

; Include a following comma in the outer object.
(parameters
  ((parameter) @parameter.inside . ","? @parameter.around) @parameter.around)
(function_value_parameters
  ((function_value_parameter) @parameter.inside . ","? @parameter.around) @parameter.around)
(argument_list
  ((_) @parameter.inside . ","? @parameter.around) @parameter.around)
(type_parameters
  ((type_parameter) @parameter.inside . ","? @parameter.around) @parameter.around)
(type_arguments
  ((_) @parameter.inside . ","? @parameter.around) @parameter.around)

[(line_comment) (block_comment)] @comment.inside
(line_comment)+ @comment.around
(block_comment) @comment.around
