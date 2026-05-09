use rmcp::model::{CallToolResult, JsonObject};

pub fn schema_inspect_schema() -> JsonObject {
    serde_json::from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "domain": { "type": "string", "enum": ["blueprint"] },
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
    if domain != "blueprint" {
        return CallToolResult::structured_error(serde_json::json!({
            "isError": true,
            "code": "UNKNOWN_DOMAIN",
            "message": format!("Unknown schema domain: {domain}"),
            "availableDomains": ["blueprint"],
            "retryable": false
        }));
    }

    let Some(tool) = args.get("tool").and_then(|value| value.as_str()) else {
        return invalid_argument_result("schema.inspect requires tool.");
    };
    if tool != "blueprint.graph.edit" {
        return CallToolResult::structured_error(serde_json::json!({
            "isError": true,
            "code": "UNKNOWN_TOOL",
            "message": format!("Unknown schema tool for domain blueprint: {tool}"),
            "availableTools": ["blueprint.graph.edit"],
            "retryable": false
        }));
    }

    let includes = match parse_schema_inspect_includes(args) {
        Ok(value) => value,
        Err(error) => return error,
    };

    if let Some(operation) = args.get("operation").and_then(|value| value.as_str()) {
        let Some(payload) = blueprint_graph_edit_operation_schema(operation, &includes) else {
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
