use rmcp::model::{CallToolResult, JsonObject};

pub fn schema_inspect_schema() -> JsonObject {
    serde_json::from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "domain": { "type": "string", "enum": ["blueprint", "material", "pcg"] },
            "tool": { "type": "string", "minLength": 1 },
            "operation": {
                "type": "string",
                "description": "Optional operation or command name within the tool."
            },
            "include": {
                "type": "array",
                "items": {
                    "type": "string",
                    "enum": ["summary", "schema", "examples", "errors", "notes"]
                },
                "default": ["summary", "schema"]
            }
        },
        "required": ["domain", "tool"],
        "additionalProperties": false
    }))
    .expect("valid schema")
}

pub fn call_schema_inspect(args: &JsonObject) -> CallToolResult {
    let Some(domain) = args.get("domain").and_then(|value| value.as_str()) else {
        return invalid_argument_result("schema.inspect requires domain.");
    };
    if domain != "blueprint" && domain != "material" && domain != "pcg" {
        return CallToolResult::structured_error(serde_json::json!({
            "isError": true,
            "code": "UNKNOWN_DOMAIN",
            "message": format!("Unknown schema domain: {domain}"),
            "availableDomains": ["blueprint", "material", "pcg"],
            "retryable": false
        }));
    }

    let Some(tool) = args.get("tool").and_then(|value| value.as_str()) else {
        return invalid_argument_result("schema.inspect requires tool.");
    };
    if domain == "pcg" {
        if !matches!(tool, "pcg.graph.edit" | "pcg.parameter.edit") {
            return CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "UNKNOWN_TOOL",
                "message": format!("Unknown schema tool for domain pcg: {tool}"),
                "availableTools": ["pcg.graph.edit", "pcg.parameter.edit"],
                "retryable": false
            }));
        }
        let includes = match parse_schema_inspect_includes(args) {
            Ok(value) => value,
            Err(error) => return error,
        };
        return match tool {
            "pcg.graph.edit" => call_pcg_graph_edit_schema_inspect(args, &includes),
            "pcg.parameter.edit" => call_pcg_parameter_edit_schema_inspect(args, &includes),
            _ => unreachable!("validated pcg schema.inspect tool"),
        };
    }
    if domain == "material" {
        if tool != "material.graph.edit" {
            return CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "UNKNOWN_TOOL",
                "message": format!("Unknown schema tool for domain material: {tool}"),
                "availableTools": ["material.graph.edit"],
                "retryable": false
            }));
        }
        let includes = match parse_schema_inspect_includes(args) {
            Ok(value) => value,
            Err(error) => return error,
        };
        return call_material_graph_edit_schema_inspect(args, &includes);
    }

    if !matches!(tool, "blueprint.graph.edit" | "blueprint.member.edit") {
        return CallToolResult::structured_error(serde_json::json!({
            "isError": true,
            "code": "UNKNOWN_TOOL",
            "message": format!("Unknown schema tool for domain blueprint: {tool}"),
            "availableTools": ["blueprint.graph.edit", "blueprint.member.edit"],
            "retryable": false
        }));
    }

    let includes = match parse_schema_inspect_includes(args) {
        Ok(value) => value,
        Err(error) => return error,
    };

    match tool {
        "blueprint.graph.edit" => call_graph_edit_schema_inspect(args, &includes),
        "blueprint.member.edit" => call_member_edit_schema_inspect(args, &includes),
        _ => unreachable!("validated schema.inspect tool"),
    }
}

fn call_graph_edit_schema_inspect(args: &JsonObject, includes: &[String]) -> CallToolResult {
    if let Some(operation) = args.get("operation").and_then(|value| value.as_str()) {
        let Some(payload) = blueprint_graph_edit_operation_schema(operation, includes) else {
            return CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "UNKNOWN_OPERATION",
                "message": format!("Unknown operation for blueprint.graph.edit: {operation}"),
                "availableOperations": blueprint_graph_edit_operation_names(),
                "retryable": false
            }));
        };
        return CallToolResult::structured(payload);
    }

    CallToolResult::structured(serde_json::json!({
        "domain": "blueprint",
        "tool": "blueprint.graph.edit",
        "operations": blueprint_graph_edit_operation_index(),
        "source": {
            "document": "docs/blueprint/graph-edit.md",
            "section": "Command Classification"
        }
    }))
}

fn call_material_graph_edit_schema_inspect(
    args: &JsonObject,
    includes: &[String],
) -> CallToolResult {
    if let Some(operation) = args.get("operation").and_then(|value| value.as_str()) {
        let Some(payload) = material_graph_edit_operation_schema(operation, includes) else {
            return CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "UNKNOWN_OPERATION",
                "message": format!("Unknown operation for material.graph.edit: {operation}"),
                "availableOperations": material_graph_edit_operation_names(),
                "retryable": false
            }));
        };
        return CallToolResult::structured(payload);
    }

    CallToolResult::structured(serde_json::json!({
        "domain": "material",
        "tool": "material.graph.edit",
        "operations": material_graph_edit_operation_index(),
        "source": {
            "document": "UE Material Editor palette and expression graph actions",
            "section": "Material graph edit command set"
        }
    }))
}

fn call_pcg_graph_edit_schema_inspect(args: &JsonObject, includes: &[String]) -> CallToolResult {
    if let Some(operation) = args.get("operation").and_then(|value| value.as_str()) {
        let Some(payload) = pcg_graph_edit_operation_schema(operation, includes) else {
            return CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "UNKNOWN_OPERATION",
                "message": format!("Unknown operation for pcg.graph.edit: {operation}"),
                "availableOperations": pcg_graph_edit_operation_names(),
                "retryable": false
            }));
        };
        return CallToolResult::structured(payload);
    }

    CallToolResult::structured(serde_json::json!({
        "domain": "pcg",
        "tool": "pcg.graph.edit",
        "operations": pcg_graph_edit_operation_index(),
        "source": {
            "document": "UE PCG editor schema actions",
            "section": "PCG graph edit command set"
        }
    }))
}

fn call_pcg_parameter_edit_schema_inspect(
    args: &JsonObject,
    includes: &[String],
) -> CallToolResult {
    if let Some(operation) = args.get("operation").and_then(|value| value.as_str()) {
        let Some(payload) = pcg_parameter_edit_operation_schema(operation, includes) else {
            return CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "UNKNOWN_OPERATION",
                "message": format!("Unknown operation for pcg.parameter.edit: {operation}"),
                "availableOperations": pcg_parameter_edit_operation_names(),
                "retryable": false
            }));
        };
        return CallToolResult::structured(payload);
    }

    CallToolResult::structured(serde_json::json!({
        "domain": "pcg",
        "tool": "pcg.parameter.edit",
        "operations": pcg_parameter_edit_operation_index(),
        "source": {
            "document": "UE PCG graph User Parameters",
            "section": "FInstancedPropertyBag operations"
        }
    }))
}

fn call_member_edit_schema_inspect(args: &JsonObject, includes: &[String]) -> CallToolResult {
    if let Some(operation) = args.get("operation").and_then(|value| value.as_str()) {
        let Some(payload) = blueprint_member_edit_operation_schema(operation, includes) else {
            return CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "UNKNOWN_OPERATION",
                "message": format!("Unknown operation for blueprint.member.edit: {operation}"),
                "availableOperations": blueprint_member_edit_operation_names(),
                "retryable": false
            }));
        };
        return CallToolResult::structured(payload);
    }

    CallToolResult::structured(serde_json::json!({
        "domain": "blueprint",
        "tool": "blueprint.member.edit",
        "operations": blueprint_member_edit_operation_index(),
        "source": {
            "document": "docs/BLUEPRINT_INTERFACE_DESIGN.md",
            "section": "Member domains"
        }
    }))
}

fn invalid_argument_result(message: impl Into<String>) -> CallToolResult {
    CallToolResult::structured_error(serde_json::json!({
        "isError": true,
        "code": "INVALID_ARGUMENT",
        "message": message.into(),
        "retryable": false,
    }))
}

fn parse_schema_inspect_includes(args: &JsonObject) -> Result<Vec<String>, CallToolResult> {
    let allowed = ["summary", "schema", "examples", "errors", "notes"];
    let Some(include_value) = args.get("include") else {
        return Ok(vec!["summary".to_string(), "schema".to_string()]);
    };
    let Some(include_items) = include_value.as_array() else {
        return Err(invalid_argument_result(
            "schema.inspect include must be an array.",
        ));
    };

    let mut includes = Vec::new();
    for value in include_items {
        let Some(include) = value.as_str() else {
            return Err(invalid_argument_result(
                "schema.inspect include entries must be strings.",
            ));
        };
        if !allowed.contains(&include) {
            return Err(CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "UNSUPPORTED_INCLUDE",
                "message": format!("Unsupported schema.inspect include: {include}"),
                "availableIncludes": allowed,
                "retryable": false
            })));
        }
        if !includes.iter().any(|existing| existing == include) {
            includes.push(include.to_string());
        }
    }
    Ok(includes)
}

fn schema_include_requested(includes: &[String], section: &str) -> bool {
    includes.iter().any(|include| include == section)
}

fn blueprint_member_edit_operation_names() -> Vec<&'static str> {
    vec![
        "variable.create",
        "variable.update",
        "variable.rename",
        "variable.reorder",
        "variable.setDefault",
        "variable.delete",
        "function.create",
        "function.rename",
        "function.setFlags",
        "function.delete",
        "macro.create",
        "macro.rename",
        "macro.delete",
        "dispatcher.create",
        "dispatcher.rename",
        "dispatcher.delete",
        "event.create",
        "event.updateSignature",
        "event.addInput",
        "event.setFlags",
        "event.rename",
        "event.delete",
        "component.create",
        "component.update",
        "component.rename",
        "component.reparent",
        "component.reorder",
        "component.delete",
    ]
}

fn blueprint_member_edit_operation_index() -> Vec<serde_json::Value> {
    blueprint_member_edit_operation_names()
        .into_iter()
        .map(|name| {
            let (member_kind, operation) = split_member_operation(name)
                .expect("member edit operation names use memberKind.operation");
            serde_json::json!({
                "name": name,
                "memberKind": member_kind,
                "operation": operation,
                "category": member_kind,
                "summary": member_edit_operation_summary(member_kind, operation)
            })
        })
        .collect()
}

fn split_member_operation(operation: &str) -> Option<(&str, &str)> {
    operation.split_once('.')
}

fn member_edit_operation_summary(member_kind: &str, operation: &str) -> &'static str {
    match (member_kind, operation) {
        ("variable", "create") => "Create one Blueprint variable.",
        ("variable", "update") => {
            "Update variable metadata, type, default, or replication settings."
        }
        ("variable", "rename") => "Rename one Blueprint variable.",
        ("variable", "reorder") => "Move one variable before or after another variable.",
        ("variable", "setDefault") => "Set one Blueprint variable default value.",
        ("variable", "delete") => "Delete one Blueprint variable.",
        ("function", "create") => "Create one Blueprint function graph and signature.",
        ("function", "rename") => "Rename one Blueprint function.",
        ("function", "setFlags") => "Update Blueprint function flags and metadata.",
        ("function", "delete") => "Delete one Blueprint function.",
        ("macro", "create") => "Create one Blueprint macro graph and signature.",
        ("macro", "rename") => "Rename one Blueprint macro.",
        ("macro", "delete") => "Delete one Blueprint macro.",
        ("dispatcher", "create") => "Create one Blueprint event dispatcher.",
        ("dispatcher", "rename") => "Rename one Blueprint event dispatcher.",
        ("dispatcher", "delete") => "Delete one Blueprint event dispatcher.",
        ("event", "create") => "Create one custom event node and signature.",
        ("event", "updateSignature") => "Replace a custom event signature.",
        ("event", "addInput") => "Add one input parameter to a custom event.",
        ("event", "setFlags") => "Update custom event replication flags.",
        ("event", "rename") => "Rename one custom event.",
        ("event", "delete") => "Delete one custom event.",
        ("component", "create") => "Create one Blueprint component.",
        ("component", "update") => "Update component defaults or editor properties.",
        ("component", "rename") => "Rename one Blueprint component.",
        ("component", "reparent") => "Change one component's attachment parent.",
        ("component", "reorder") => "Move one component before or after another component.",
        ("component", "delete") => "Delete one Blueprint component.",
        _ => "Edit one Blueprint member.",
    }
}

fn blueprint_member_edit_operation_schema(
    operation_name: &str,
    includes: &[String],
) -> Option<serde_json::Value> {
    let (member_kind, operation) = split_member_operation(operation_name)?;
    if !blueprint_member_edit_operation_names()
        .iter()
        .any(|name| name == &operation_name)
    {
        return None;
    }

    let schema = serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": {"type":"string","minLength":1},
            "memberKind": {"const": member_kind},
            "operation": {"const": operation},
            "args": member_edit_args_schema(member_kind, operation),
            "dryRun": {"type":"boolean","default":false},
            "returnDiff": {"type":"boolean","default":false},
            "returnDiagnostics": {"type":"boolean","default":true},
            "expectedRevision": {"type":"string"}
        },
        "required": ["assetPath", "memberKind", "operation", "args"],
        "additionalProperties": false
    });
    let examples = vec![member_edit_example(member_kind, operation)];
    let notes = vec![
        "Operation names in schema.inspect use memberKind.operation; blueprint.member.edit requests pass memberKind and operation separately.",
        "The args schema documents the stable request shape. UE may still reject invalid Blueprint-specific combinations.",
    ];

    let mut payload = serde_json::json!({
        "domain": "blueprint",
        "tool": "blueprint.member.edit",
        "operation": operation_name,
        "memberKind": member_kind,
        "category": member_kind,
        "source": {
            "document": "docs/BLUEPRINT_INTERFACE_DESIGN.md",
            "section": format!("{member_kind}.{operation}")
        }
    });
    let payload_object = payload.as_object_mut()?;
    if schema_include_requested(includes, "summary") {
        payload_object.insert(
            "summary".to_string(),
            serde_json::json!(member_edit_operation_summary(member_kind, operation)),
        );
    }
    if schema_include_requested(includes, "schema") {
        payload_object.insert("schema".to_string(), schema);
    }
    if schema_include_requested(includes, "examples") {
        payload_object.insert("examples".to_string(), serde_json::json!(examples));
    }
    if schema_include_requested(includes, "errors") {
        payload_object.insert(
            "errors".to_string(),
            serde_json::json!([
                "INVALID_ARGUMENT",
                "MEMBER_NOT_FOUND",
                "MEMBER_ALREADY_EXISTS"
            ]),
        );
    }
    if schema_include_requested(includes, "notes") {
        payload_object.insert("notes".to_string(), serde_json::json!(notes));
    }
    Some(payload)
}

fn member_edit_args_schema(member_kind: &str, operation: &str) -> serde_json::Value {
    match (member_kind, operation) {
        ("variable", "create") => serde_json::json!({
            "type":"object",
            "properties":{"variableName":{"type":"string","minLength":1},"type":{"type":"object"},"defaultValue":{}},
            "required":["variableName","type"],
            "additionalProperties":true
        }),
        ("variable", "rename") => name_schema("variableName"),
        ("variable", "setDefault") => serde_json::json!({
            "type":"object",
            "properties":{"variableName":{"type":"string","minLength":1},"defaultValue":{}},
            "required":["variableName","defaultValue"],
            "additionalProperties":false
        }),
        ("variable", "delete") => single_name_schema("variableName"),
        ("variable", "reorder") => serde_json::json!({
            "type":"object",
            "properties":{"variableName":{"type":"string","minLength":1},"targetVariableName":{"type":"string","minLength":1},"placement":{"type":"string","enum":["before","after"]}},
            "required":["variableName","targetVariableName","placement"],
            "additionalProperties":false
        }),
        ("variable", "update") => object_with_required_name("variableName"),
        ("function", "create") => function_like_create_schema("functionName"),
        ("function", "rename") => name_schema("functionName"),
        ("function", "setFlags") => object_with_required_name("functionName"),
        ("function", "delete") => single_name_schema("functionName"),
        ("macro", "create") => function_like_create_schema("macroName"),
        ("macro", "rename") => name_schema("macroName"),
        ("macro", "delete") => single_name_schema("macroName"),
        ("dispatcher", "create") => function_like_create_schema("dispatcherName"),
        ("dispatcher", "rename") => name_schema("dispatcherName"),
        ("dispatcher", "delete") => single_name_schema("dispatcherName"),
        ("event", "create") => serde_json::json!({
            "type":"object",
            "properties":{"name":{"type":"string","minLength":1},"graphName":{"type":"string","minLength":1},"inputs":{"type":"array","items":{"type":"object"}},"replication":{"type":"string"},"reliable":{"type":"boolean"},"x":{"type":"number"},"y":{"type":"number"}},
            "required":["name"],
            "additionalProperties":true
        }),
        ("event", "updateSignature") => serde_json::json!({
            "type":"object",
            "properties":{"name":{"type":"string","minLength":1},"inputs":{"type":"array","items":{"type":"object"}}},
            "required":["name","inputs"],
            "additionalProperties":false
        }),
        ("event", "addInput") => serde_json::json!({
            "type":"object",
            "properties":{"name":{"type":"string","minLength":1},"inputName":{"type":"string","minLength":1},"type":{"type":"object"},"inputType":{"type":"string"}},
            "required":["name","inputName","type"],
            "additionalProperties":true
        }),
        ("event", "setFlags") => object_with_required_name("name"),
        ("event", "rename") => name_schema("name"),
        ("event", "delete") => single_name_schema("name"),
        ("component", "create") => serde_json::json!({
            "type":"object",
            "properties":{"componentName":{"type":"string","minLength":1},"componentClassPath":{"type":"string","minLength":1},"parentComponentName":{"type":"string","minLength":1}},
            "required":["componentName","componentClassPath"],
            "additionalProperties":true
        }),
        ("component", "update") => object_with_required_name("componentName"),
        ("component", "rename") => name_schema("componentName"),
        ("component", "reparent") => serde_json::json!({
            "type":"object",
            "properties":{"componentName":{"type":"string","minLength":1},"parentComponentName":{"type":"string","minLength":1}},
            "required":["componentName","parentComponentName"],
            "additionalProperties":false
        }),
        ("component", "reorder") => serde_json::json!({
            "type":"object",
            "properties":{"componentName":{"type":"string","minLength":1},"targetComponentName":{"type":"string","minLength":1},"placement":{"type":"string","enum":["before","after"]}},
            "required":["componentName","targetComponentName","placement"],
            "additionalProperties":false
        }),
        ("component", "delete") => single_name_schema("componentName"),
        _ => serde_json::json!({"type":"object","additionalProperties":true}),
    }
}

fn single_name_schema(name_field: &str) -> serde_json::Value {
    let mut properties = serde_json::Map::new();
    properties.insert(
        name_field.to_string(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":[name_field],
        "additionalProperties":false
    })
}

fn name_schema(name_field: &str) -> serde_json::Value {
    let mut properties = serde_json::Map::new();
    properties.insert(
        name_field.to_string(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "newName".to_string(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":[name_field,"newName"],
        "additionalProperties":false
    })
}

fn object_with_required_name(name_field: &str) -> serde_json::Value {
    let mut properties = serde_json::Map::new();
    properties.insert(
        name_field.to_string(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":[name_field],
        "additionalProperties":true
    })
}

fn function_like_create_schema(name_field: &str) -> serde_json::Value {
    let mut properties = serde_json::Map::new();
    properties.insert(
        name_field.to_string(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "inputs".to_string(),
        serde_json::json!({"type":"array","items":{"type":"object"}}),
    );
    properties.insert(
        "outputs".to_string(),
        serde_json::json!({"type":"array","items":{"type":"object"}}),
    );
    properties.insert("category".to_string(), serde_json::json!({"type":"string"}));
    properties.insert("tooltip".to_string(), serde_json::json!({"type":"string"}));
    serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":[name_field],
        "additionalProperties":true
    })
}

fn member_edit_example(member_kind: &str, operation: &str) -> serde_json::Value {
    match (member_kind, operation) {
        ("variable", "create") => {
            serde_json::json!({"assetPath":"/Game/BP_Test","memberKind":"variable","operation":"create","args":{"variableName":"bIsReady","type":{"category":"bool"},"defaultValue":"false"}})
        }
        ("event", "addInput") => {
            serde_json::json!({"assetPath":"/Game/BP_Test","memberKind":"event","operation":"addInput","args":{"name":"OnReady","inputName":"Count","type":{"category":"int"}}})
        }
        ("component", "create") => {
            serde_json::json!({"assetPath":"/Game/BP_Test","memberKind":"component","operation":"create","args":{"componentName":"VisualMesh","componentClassPath":"/Script/Engine.StaticMeshComponent"}})
        }
        _ => {
            serde_json::json!({"assetPath":"/Game/BP_Test","memberKind":member_kind,"operation":operation,"args":{}})
        }
    }
}

fn blueprint_graph_edit_operation_names() -> Vec<&'static str> {
    vec![
        "addFromPalette",
        "connect",
        "disconnect",
        "breakLinks",
        "setPinDefault",
        "removeNode",
        "moveNode",
        "addCommentBox",
        "addReroute",
        "setNodeComment",
        "duplicateNode",
        "reconstructNode",
        "setNodeEnabled",
    ]
}

fn blueprint_graph_edit_operation_index() -> Vec<serde_json::Value> {
    vec![
        serde_json::json!({"name":"addFromPalette","category":"core","summary":"Execute one selected blueprint.palette entry."}),
        serde_json::json!({"name":"connect","category":"core","summary":"Create one explicit pin link."}),
        serde_json::json!({"name":"disconnect","category":"core","summary":"Remove one explicit pin link."}),
        serde_json::json!({"name":"breakLinks","category":"core","summary":"Remove all links from one pin."}),
        serde_json::json!({"name":"setPinDefault","category":"core","summary":"Set one editable pin default."}),
        serde_json::json!({"name":"removeNode","category":"core","summary":"Remove one node."}),
        serde_json::json!({"name":"moveNode","category":"core","summary":"Move one node by absolute position or delta."}),
        serde_json::json!({"name":"addCommentBox","category":"annotation","summary":"Create one comment box."}),
        serde_json::json!({"name":"addReroute","category":"layout","summary":"Create one reroute node for wire organization."}),
        serde_json::json!({"name":"setNodeComment","category":"annotation","summary":"Set one node comment."}),
        serde_json::json!({"name":"duplicateNode","category":"advanced","summary":"Duplicate one node."}),
        serde_json::json!({"name":"reconstructNode","category":"advanced","summary":"Ask UE to reconstruct one node."}),
        serde_json::json!({"name":"setNodeEnabled","category":"advanced","summary":"Set one node enabled state."}),
    ]
}

fn blueprint_graph_edit_operation_schema(
    operation: &str,
    includes: &[String],
) -> Option<serde_json::Value> {
    let (category, summary, schema, examples, errors, notes) = match operation {
        "addFromPalette" => (
            "core",
            "Execute one selected blueprint.palette entry.",
            serde_json::json!({
                "type":"object",
                "properties":{
                    "kind":{"const":"addFromPalette"},
                    "entry":{"type":"object","properties":{"id":{"type":"string","minLength":1}},"required":["id"],"additionalProperties":true},
                    "position":{"$ref":"#/$defs/position"},
                    "alias":{"type":"string","minLength":1},
                    "fromPins":{"type":"array","items":{"type":"object"}},
                    "contextSensitive":{"type":"boolean"}
                },
                "required":["kind","entry"],
                "additionalProperties":false
            }),
            vec![
                serde_json::json!({"kind":"addFromPalette","entry":{"id":"palette:..."},"position":{"x":400,"y":200},"alias":"branch"}),
            ],
            vec!["PALETTE_ENTRY_NOT_EXECUTABLE"],
            vec!["entry should be the full object returned by blueprint.palette."],
        ),
        "connect" => (
            "core",
            "Create one explicit pin link.",
            serde_json::json!({
                "type":"object",
                "properties":{"kind":{"const":"connect"},"from":{"$ref":"#/$defs/pinRef"},"to":{"$ref":"#/$defs/pinRef"}},
                "required":["kind","from","to"],
                "additionalProperties":false
            }),
            vec![
                serde_json::json!({"kind":"connect","from":{"node":{"alias":"branch"},"pin":"then"},"to":{"node":{"alias":"print"},"pin":"execute"}}),
            ],
            vec!["PIN_NOT_FOUND", "CONNECT_PIN_TYPE_MISMATCH"],
            vec!["connect does not insert conversion, cast, or promotion nodes."],
        ),
        "disconnect" => (
            "core",
            "Remove one explicit pin link.",
            serde_json::json!({
                "type":"object",
                "properties":{"kind":{"const":"disconnect"},"from":{"$ref":"#/$defs/pinRef"},"to":{"$ref":"#/$defs/pinRef"}},
                "required":["kind","from","to"],
                "additionalProperties":false
            }),
            vec![
                serde_json::json!({"kind":"disconnect","from":{"node":{"id":"node-1"},"pin":"then"},"to":{"node":{"id":"node-2"},"pin":"execute"}}),
            ],
            vec!["PIN_NOT_FOUND"],
            vec![],
        ),
        "breakLinks" => (
            "core",
            "Remove all links from one pin.",
            serde_json::json!({
                "type":"object",
                "properties":{"kind":{"const":"breakLinks"},"target":{"$ref":"#/$defs/pinRef"}},
                "required":["kind","target"],
                "additionalProperties":false
            }),
            vec![
                serde_json::json!({"kind":"breakLinks","target":{"node":{"id":"node-1"},"pin":"InString"}}),
            ],
            vec!["PIN_NOT_FOUND"],
            vec!["breakLinks does not change the pin default value."],
        ),
        "setPinDefault" => (
            "core",
            "Set one editable pin default.",
            serde_json::json!({
                "type":"object",
                "properties":{"kind":{"const":"setPinDefault"},"target":{"$ref":"#/$defs/pinRef"},"value":{}},
                "required":["kind","target","value"],
                "additionalProperties":false
            }),
            vec![
                serde_json::json!({"kind":"setPinDefault","target":{"node":{"alias":"print"},"pin":"InString"},"value":"Hello"}),
                serde_json::json!({"kind":"setPinDefault","target":{"node":{"alias":"spawn"},"pin":"Class"},"value":{"object":"/Game/BP_Coin.BP_Coin_C"}}),
            ],
            vec!["PIN_NOT_FOUND", "PIN_DEFAULT_NOT_EDITABLE"],
            vec!["setPinDefault must not implicitly break links."],
        ),
        "removeNode" => (
            "core",
            "Remove one node.",
            serde_json::json!({
                "type":"object",
                "properties":{"kind":{"const":"removeNode"},"node":{"$ref":"#/$defs/nodeRef"}},
                "required":["kind","node"],
                "additionalProperties":false
            }),
            vec![serde_json::json!({"kind":"removeNode","node":{"id":"node-1"}})],
            vec!["NODE_NOT_FOUND"],
            vec!["removeNode does not auto-heal surrounding graph structure."],
        ),
        "moveNode" => (
            "core",
            "Move one node by absolute position or delta.",
            serde_json::json!({
                "type":"object",
                "properties":{"kind":{"const":"moveNode"},"node":{"$ref":"#/$defs/nodeRef"},"position":{"$ref":"#/$defs/position"},"delta":{"$ref":"#/$defs/position"}},
                "required":["kind","node"],
                "additionalProperties":false
            }),
            vec![
                serde_json::json!({"kind":"moveNode","node":{"id":"node-1"},"position":{"x":400,"y":240}}),
                serde_json::json!({"kind":"moveNode","node":{"id":"node-1"},"delta":{"x":120,"y":0}}),
            ],
            vec!["NODE_NOT_FOUND"],
            vec!["Use exactly one of position or delta."],
        ),
        "addCommentBox" | "addReroute" | "setNodeComment" | "duplicateNode" | "reconstructNode"
        | "setNodeEnabled" => {
            let (category, summary) = match operation {
                "addCommentBox" => ("annotation", "Create one comment box."),
                "addReroute" => ("layout", "Create one reroute node for wire organization."),
                "setNodeComment" => ("annotation", "Set one node comment."),
                "duplicateNode" => ("advanced", "Duplicate one node."),
                "reconstructNode" => ("advanced", "Ask UE to reconstruct one node."),
                "setNodeEnabled" => ("advanced", "Set one node enabled state."),
                _ => return None,
            };
            (
                category,
                summary,
                serde_json::json!({}),
                Vec::new(),
                Vec::new(),
                vec!["Secondary command. Not part of the core graph edit command set."],
            )
        }
        _ => return None,
    };

    let mut payload = serde_json::json!({
        "domain": "blueprint",
        "tool": "blueprint.graph.edit",
        "operation": operation,
        "category": category,
        "source": {
            "document": "docs/blueprint/graph-edit.md",
            "section": operation
        }
    });
    let payload_object = payload.as_object_mut()?;
    if schema_include_requested(includes, "summary") {
        payload_object.insert("summary".to_string(), serde_json::json!(summary));
    }
    if schema_include_requested(includes, "schema") {
        payload_object.insert("schema".to_string(), add_graph_edit_schema_defs(schema));
    }
    if schema_include_requested(includes, "examples") {
        payload_object.insert("examples".to_string(), serde_json::json!(examples));
    }
    if schema_include_requested(includes, "errors") {
        payload_object.insert("errors".to_string(), serde_json::json!(errors));
    }
    if schema_include_requested(includes, "notes") {
        payload_object.insert("notes".to_string(), serde_json::json!(notes));
    }
    Some(payload)
}

fn add_graph_edit_schema_defs(mut schema: serde_json::Value) -> serde_json::Value {
    if let Some(object) = schema.as_object_mut() {
        object.insert(
            "$defs".to_string(),
            serde_json::json!({
                "nodeRef": {
                    "oneOf": [
                        {"type":"object","properties":{"id":{"type":"string","minLength":1}},"required":["id"],"additionalProperties":false},
                        {"type":"object","properties":{"alias":{"type":"string","minLength":1}},"required":["alias"],"additionalProperties":false}
                    ]
                },
                "pinRef": {
                    "type":"object",
                    "properties":{"node":{"$ref":"#/$defs/nodeRef"},"pin":{"type":"string","minLength":1}},
                    "required":["node","pin"],
                    "additionalProperties":false
                },
                "position": {
                    "type":"object",
                    "properties":{"x":{"type":"number"},"y":{"type":"number"}},
                    "required":["x","y"],
                    "additionalProperties":false
                }
            }),
        );
    }
    schema
}

fn pcg_graph_edit_operation_names() -> Vec<&'static str> {
    vec![
        "addFromPalette",
        "removeNode",
        "moveNode",
        "connect",
        "disconnect",
        "setPinDefault",
        "setNodeProperty",
    ]
}

fn pcg_graph_edit_operation_index() -> Vec<serde_json::Value> {
    vec![
        serde_json::json!({"name":"addFromPalette","category":"core","summary":"Create one PCG node from a selected pcg.palette entry."}),
        serde_json::json!({"name":"removeNode","category":"core","summary":"Remove one PCG node."}),
        serde_json::json!({"name":"moveNode","category":"layout","summary":"Move one PCG node by absolute position or delta."}),
        serde_json::json!({"name":"connect","category":"core","summary":"Create one explicit PCG pin edge."}),
        serde_json::json!({"name":"disconnect","category":"core","summary":"Remove one explicit PCG pin edge."}),
        serde_json::json!({"name":"setPinDefault","category":"core","summary":"Set one PCG pin default value."}),
        serde_json::json!({"name":"setNodeProperty","category":"core","summary":"Set one property on a PCG node settings object."}),
    ]
}

fn material_graph_edit_operation_names() -> Vec<&'static str> {
    vec![
        "addFromPalette",
        "removeNode",
        "moveNode",
        "connect",
        "disconnect",
        "breakPinLinks",
    ]
}

fn material_graph_edit_operation_index() -> Vec<serde_json::Value> {
    vec![
        serde_json::json!({"name":"addFromPalette","category":"core","summary":"Create one Material expression node from a selected material.palette entry."}),
        serde_json::json!({"name":"removeNode","category":"core","summary":"Remove one Material expression node."}),
        serde_json::json!({"name":"moveNode","category":"core","summary":"Move one Material expression node by absolute position or delta."}),
        serde_json::json!({"name":"connect","category":"core","summary":"Create one explicit Material expression or root pin link."}),
        serde_json::json!({"name":"disconnect","category":"core","summary":"Remove one explicit Material expression or root pin link."}),
        serde_json::json!({"name":"breakPinLinks","category":"core","summary":"Remove all links from one Material expression or root pin."}),
    ]
}

fn pcg_parameter_edit_operation_names() -> Vec<&'static str> {
    vec!["create", "update", "rename", "delete", "setDefault"]
}

fn pcg_parameter_edit_operation_index() -> Vec<serde_json::Value> {
    vec![
        serde_json::json!({"name":"create","category":"schema","summary":"Add a graph user parameter to the PCG graph Parameters bag."}),
        serde_json::json!({"name":"update","category":"schema","summary":"Change an existing graph user parameter type/container while preserving values when UE can migrate them."}),
        serde_json::json!({"name":"rename","category":"schema","summary":"Rename a graph user parameter and let PCG update parameter getter references."}),
        serde_json::json!({"name":"delete","category":"schema","summary":"Remove a graph user parameter from the Parameters bag."}),
        serde_json::json!({"name":"setDefault","category":"value","summary":"Set a graph user parameter default value using UE serialized value text."}),
    ]
}

fn pcg_parameter_edit_operation_schema(
    operation: &str,
    includes: &[String],
) -> Option<serde_json::Value> {
    let type_schema = serde_json::json!({
        "type": "string",
        "enum": ["Bool", "Byte", "Int32", "Int64", "UInt32", "UInt64", "Float", "Double", "Name", "String", "Text", "Enum", "Struct", "Object", "SoftObject", "Class", "SoftClass"]
    });
    let (category, summary, schema, examples, errors, notes) = match operation {
        "create" => (
            "schema",
            "Add a graph user parameter to the PCG graph Parameters bag.",
            serde_json::json!({"type":"object","properties":{"name":{"type":"string","minLength":1},"type":type_schema,"container":{"type":"string","enum":["None","Array","Set"],"default":"None"},"typeObject":{"type":"string","minLength":1},"value":{"type":"string","description":"Optional UE serialized default value, set with a follow-up setDefault if omitted."}},"required":["name","type"],"additionalProperties":false}),
            vec![serde_json::json!({"assetPath":"/Game/PCG/PCG_Forest","operation":"create","args":{"name":"DensityScale","type":"Double","value":"1.0"}})],
            vec!["INVALID_ARGUMENT", "PARAMETER_EDIT_FAILED"],
            vec!["This creates the parameter itself. Use pcg.palette afterwards to create a parameterGetter node for it."],
        ),
        "update" => (
            "schema",
            "Change an existing graph user parameter type/container while preserving values when UE can migrate them.",
            serde_json::json!({"type":"object","properties":{"name":{"type":"string","minLength":1},"type":type_schema,"container":{"type":"string","enum":["None","Array","Set"],"default":"None"},"typeObject":{"type":"string","minLength":1}},"required":["name","type"],"additionalProperties":false}),
            vec![serde_json::json!({"assetPath":"/Game/PCG/PCG_Forest","operation":"update","args":{"name":"DensityScale","type":"Float"}})],
            vec!["INVALID_ARGUMENT", "PARAMETER_EDIT_FAILED"],
            vec!["UE may migrate compatible numeric values when the type changes; incompatible values reset according to PropertyBag behavior."],
        ),
        "rename" => (
            "schema",
            "Rename a graph user parameter and let PCG update parameter getter references.",
            serde_json::json!({"type":"object","properties":{"name":{"type":"string","minLength":1},"newName":{"type":"string","minLength":1}},"required":["name","newName"],"additionalProperties":false}),
            vec![serde_json::json!({"assetPath":"/Game/PCG/PCG_Forest","operation":"rename","args":{"name":"DensityScale","newName":"DensityMultiplier"}})],
            vec!["INVALID_ARGUMENT", "PARAMETER_EDIT_FAILED"],
            vec!["Uses UE PCG RenameUserParameter so getter nodes can track the rename by property id."],
        ),
        "delete" => (
            "schema",
            "Remove a graph user parameter from the Parameters bag.",
            serde_json::json!({"type":"object","properties":{"name":{"type":"string","minLength":1}},"required":["name"],"additionalProperties":false}),
            vec![serde_json::json!({"assetPath":"/Game/PCG/PCG_Forest","operation":"delete","args":{"name":"DensityScale"}})],
            vec!["INVALID_ARGUMENT", "PARAMETER_EDIT_FAILED"],
            vec!["Deleting a used parameter follows PCG graph parameter change propagation and may remove/repair parameter getter usage according to UE behavior."],
        ),
        "setDefault" => (
            "value",
            "Set a graph user parameter default value using UE serialized value text.",
            serde_json::json!({"type":"object","properties":{"name":{"type":"string","minLength":1},"value":{"type":"string","description":"UE serialized text accepted by FInstancedPropertyBag::SetValueSerializedString."}},"required":["name","value"],"additionalProperties":false}),
            vec![serde_json::json!({"assetPath":"/Game/PCG/PCG_Forest","operation":"setDefault","args":{"name":"DensityScale","value":"0.75"}})],
            vec!["INVALID_ARGUMENT", "PARAMETER_EDIT_FAILED"],
            vec!["Inspect the current parameter first with pcg.parameter.inspect to see type and serialized default format."],
        ),
        _ => return None,
    };

    let mut payload = serde_json::json!({
        "domain": "pcg",
        "tool": "pcg.parameter.edit",
        "operation": operation,
        "category": category,
        "source": {
            "document": "UE PCG graph User Parameters",
            "section": operation
        }
    });
    let payload_object = payload.as_object_mut()?;
    if schema_include_requested(includes, "summary") {
        payload_object.insert("summary".to_string(), serde_json::json!(summary));
    }
    if schema_include_requested(includes, "schema") {
        payload_object.insert("schema".to_string(), schema);
    }
    if schema_include_requested(includes, "examples") {
        payload_object.insert("examples".to_string(), serde_json::json!(examples));
    }
    if schema_include_requested(includes, "errors") {
        payload_object.insert("errors".to_string(), serde_json::json!(errors));
    }
    if schema_include_requested(includes, "notes") {
        payload_object.insert("notes".to_string(), serde_json::json!(notes));
    }
    Some(payload)
}

fn pcg_graph_edit_operation_schema(
    operation: &str,
    includes: &[String],
) -> Option<serde_json::Value> {
    let (category, summary, schema, examples, errors, notes) = match operation {
        "addFromPalette" => (
            "core",
            "Create one PCG node from a selected pcg.palette entry.",
            serde_json::json!({
                "type":"object",
                "properties":{
                    "kind":{"const":"addFromPalette"},
                    "entry":{"type":"object","properties":{"id":{"type":"string","minLength":1},"kind":{"type":"string"},"payload":{"type":"object"},"executable":{"type":"boolean"}},"required":["id","kind","payload"],"additionalProperties":true},
                    "position":{"$ref":"#/$defs/position"},
                    "anchor":{"$ref":"#/$defs/nodeRef"},
                    "behavior":{"type":"string","enum":["normal","copy","instance","subgraph","loop"]},
                    "alias":{"type":"string","minLength":1}
                },
                "required":["kind","entry"],
                "additionalProperties":false
            }),
            vec![serde_json::json!({"kind":"addFromPalette","entry":{"id":"pcg.palette:...","kind":"native","payload":{"settingsClass":"/Script/PCG.PCGStaticMeshSpawnerSettings"}},"position":{"x":400,"y":160},"alias":"spawnMeshes"})],
            vec!["PALETTE_ENTRY_NOT_EXECUTABLE", "INVALID_PALETTE_ENTRY"],
            vec!["Use pcg.palette first and pass the full selected entry. Do not guess settings classes in public calls."],
        ),
        "removeNode" => (
            "core",
            "Remove one PCG node.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"removeNode"},"node":{"$ref":"#/$defs/nodeRef"}},"required":["kind","node"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"removeNode","node":{"id":"node-1"}})],
            vec!["NODE_NOT_FOUND"],
            vec!["removeNode does not auto-heal surrounding graph structure."],
        ),
        "moveNode" => (
            "layout",
            "Move one PCG node by absolute position or delta.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"moveNode"},"node":{"$ref":"#/$defs/nodeRef"},"position":{"$ref":"#/$defs/position"},"delta":{"$ref":"#/$defs/position"}},"required":["kind","node"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"moveNode","node":{"alias":"spawnMeshes"},"delta":{"x":240,"y":0}})],
            vec!["NODE_NOT_FOUND"],
            vec!["Use exactly one of position or delta."],
        ),
        "connect" => (
            "core",
            "Create one explicit PCG pin edge.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"connect"},"from":{"$ref":"#/$defs/pinRef"},"to":{"$ref":"#/$defs/pinRef"},"conversionPolicy":{"type":"string","enum":["strict"],"default":"strict"}},"required":["kind","from","to"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"connect","from":{"node":{"alias":"source"},"pin":"Output"},"to":{"node":{"alias":"filter"},"pin":"Input"},"conversionPolicy":"strict"})],
            vec!["PIN_NOT_FOUND", "CONNECT_PIN_TYPE_MISMATCH"],
            vec!["conversionPolicy currently supports strict only; auto conversion/filter insertion is not implemented."],
        ),
        "disconnect" => (
            "core",
            "Remove one explicit PCG pin edge.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"disconnect"},"from":{"$ref":"#/$defs/pinRef"},"to":{"$ref":"#/$defs/pinRef"}},"required":["kind","from","to"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"disconnect","from":{"node":{"id":"node-1"},"pin":"Output"},"to":{"node":{"id":"node-2"},"pin":"Input"}})],
            vec!["PIN_NOT_FOUND"],
            vec![],
        ),
        "setPinDefault" => (
            "core",
            "Set one PCG pin default value.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"setPinDefault"},"target":{"$ref":"#/$defs/pinRef"},"value":{}},"required":["kind","target","value"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"setPinDefault","target":{"node":{"alias":"sampler"},"pin":"Density"},"value":"0.5"})],
            vec!["PIN_NOT_FOUND", "PIN_DEFAULT_NOT_EDITABLE"],
            vec![],
        ),
        "setNodeProperty" => (
            "core",
            "Set one property on a PCG node settings object.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"setNodeProperty"},"node":{"$ref":"#/$defs/nodeRef"},"property":{"type":"string","minLength":1},"value":{}},"required":["kind","node","property","value"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"setNodeProperty","node":{"alias":"sampler"},"property":"PointExtents","value":"(X=100,Y=100,Z=100)"})],
            vec!["PROPERTY_NOT_FOUND", "SETTINGS_NOT_FOUND"],
            vec!["Use pcg.node.inspect to inspect editable settings properties before calling this operation."],
        ),
        _ => return None,
    };

    let mut payload = serde_json::json!({
        "domain": "pcg",
        "tool": "pcg.graph.edit",
        "operation": operation,
        "category": category,
        "source": {
            "document": "UE PCG editor schema actions",
            "section": operation
        }
    });
    let payload_object = payload.as_object_mut()?;
    if schema_include_requested(includes, "summary") {
        payload_object.insert("summary".to_string(), serde_json::json!(summary));
    }
    if schema_include_requested(includes, "schema") {
        payload_object.insert("schema".to_string(), add_graph_edit_schema_defs(schema));
    }
    if schema_include_requested(includes, "examples") {
        payload_object.insert("examples".to_string(), serde_json::json!(examples));
    }
    if schema_include_requested(includes, "errors") {
        payload_object.insert("errors".to_string(), serde_json::json!(errors));
    }
    if schema_include_requested(includes, "notes") {
        payload_object.insert("notes".to_string(), serde_json::json!(notes));
    }
    Some(payload)
}

fn material_graph_edit_operation_schema(
    operation: &str,
    includes: &[String],
) -> Option<serde_json::Value> {
    let (category, summary, schema, examples, errors, notes) = match operation {
        "addFromPalette" => (
            "core",
            "Create one Material expression node from a selected material.palette entry.",
            serde_json::json!({
                "type":"object",
                "properties":{
                    "kind":{"const":"addFromPalette"},
                    "entry":{"type":"object","properties":{"id":{"type":"string","minLength":1},"kind":{"const":"expression"},"payload":{"type":"object","properties":{"nodeClassPath":{"type":"string","minLength":1}},"required":["nodeClassPath"],"additionalProperties":true},"executable":{"type":"boolean"}},"required":["id","payload"],"additionalProperties":true},
                    "position":{"$ref":"#/$defs/position"},
                    "anchor":{"$ref":"#/$defs/nodeRef"},
                    "near":{"$ref":"#/$defs/nodeRef"},
                    "parameterName":{"type":"string","minLength":1},
                    "alias":{"type":"string","minLength":1}
                },
                "required":["kind","entry"],
                "additionalProperties":false
            }),
            vec![serde_json::json!({"kind":"addFromPalette","entry":{"id":"material.palette:...","kind":"expression","payload":{"nodeClassPath":"/Script/Engine.MaterialExpressionMultiply"}},"position":{"x":240,"y":120},"alias":"multiply"})],
            vec!["PALETTE_ENTRY_NOT_EXECUTABLE", "INVALID_PALETTE_ENTRY"],
            vec!["Use material.palette first and pass the full selected entry. Do not guess expression classes in public calls."],
        ),
        "removeNode" => (
            "core",
            "Remove one Material expression node.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"removeNode"},"node":{"$ref":"#/$defs/nodeRef"}},"required":["kind","node"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"removeNode","node":{"id":"node-1"}})],
            vec!["NODE_NOT_FOUND"],
            vec!["removeNode does not auto-heal surrounding graph structure."],
        ),
        "moveNode" => (
            "core",
            "Move one Material expression node by absolute position or delta.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"moveNode"},"node":{"$ref":"#/$defs/nodeRef"},"position":{"$ref":"#/$defs/position"},"delta":{"$ref":"#/$defs/position"}},"required":["kind","node"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"moveNode","node":{"alias":"multiply"},"delta":{"x":180,"y":0}})],
            vec!["NODE_NOT_FOUND"],
            vec!["Use exactly one of position or delta."],
        ),
        "connect" => (
            "core",
            "Create one explicit Material expression or root pin link.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"connect"},"from":{"$ref":"#/$defs/pinRef"},"to":{"$ref":"#/$defs/pinRef"}},"required":["kind","from","to"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"connect","from":{"node":{"alias":"multiply"},"pin":"Result"},"to":{"node":{"id":"__material_root__"},"pin":"Base Color"}})],
            vec!["PIN_NOT_FOUND", "CONNECT_PIN_TYPE_MISMATCH"],
            vec!["Material root inputs are addressed as node id __material_root__ with the root pin name returned by material.query."],
        ),
        "disconnect" => (
            "core",
            "Remove one explicit Material expression or root pin link.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"disconnect"},"from":{"$ref":"#/$defs/pinRef"},"to":{"$ref":"#/$defs/pinRef"}},"required":["kind","from","to"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"disconnect","from":{"node":{"id":"node-1"},"pin":"Result"},"to":{"node":{"id":"__material_root__"},"pin":"Base Color"}})],
            vec!["PIN_NOT_FOUND"],
            vec![],
        ),
        "breakPinLinks" => (
            "core",
            "Remove all links from one Material expression or root pin.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"breakPinLinks"},"target":{"$ref":"#/$defs/pinRef"}},"required":["kind","target"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"breakPinLinks","target":{"node":{"id":"__material_root__"},"pin":"Base Color"}})],
            vec!["PIN_NOT_FOUND"],
            vec!["breakPinLinks removes links only; it does not create replacement values."],
        ),
        _ => return None,
    };

    let mut payload = serde_json::json!({
        "domain": "material",
        "tool": "material.graph.edit",
        "operation": operation,
        "category": category,
        "source": {
            "document": "UE Material Editor palette and expression graph actions",
            "section": operation
        }
    });
    let payload_object = payload.as_object_mut()?;
    if schema_include_requested(includes, "summary") {
        payload_object.insert("summary".to_string(), serde_json::json!(summary));
    }
    if schema_include_requested(includes, "schema") {
        payload_object.insert("schema".to_string(), add_graph_edit_schema_defs(schema));
    }
    if schema_include_requested(includes, "examples") {
        payload_object.insert("examples".to_string(), serde_json::json!(examples));
    }
    if schema_include_requested(includes, "errors") {
        payload_object.insert("errors".to_string(), serde_json::json!(errors));
    }
    if schema_include_requested(includes, "notes") {
        payload_object.insert("notes".to_string(), serde_json::json!(notes));
    }
    Some(payload)
}
