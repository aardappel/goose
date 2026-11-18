; Qualified names use their final identifier.
(namespace_declaration name: (identifier) @name) @definition.module
(function_declaration name: (identifier) @name) @definition.function
(function_declaration name: (scoped_identifier name: (identifier) @name)) @definition.function
(extern_member name: (identifier) @name) @definition.function
(extern_member name: (scoped_identifier name: (identifier) @name)) @definition.function
(struct_declaration name: (identifier) @name) @definition.struct
(struct_declaration name: (scoped_identifier name: (identifier) @name)) @definition.struct
(enum_declaration name: (identifier) @name) @definition.enum
(enum_declaration name: (scoped_identifier name: (identifier) @name)) @definition.enum
(type_alias_declaration name: (identifier) @name) @definition.type
(type_alias_declaration name: (scoped_identifier name: (identifier) @name)) @definition.type
(enum_variant name: (identifier) @name) @definition.constant
(field_declaration name: (identifier) @name) @definition.field

; Do not add local constants to the file-level symbol list.
(source_file (variable_declaration "const" name: (identifier) @name) @definition.constant)
(source_file (variable_declaration "const" name: (scoped_identifier name: (identifier) @name)) @definition.constant)
