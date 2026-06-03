use rmcp::model::{CallToolResult, JsonObject};

const TOOL_MANIFEST_JSON: &str = include_str!("../../mcp/manifest/manifest.json");
const WIDGET_EDIT_SCHEMA_TOOL: &str = "widget.edit";

pub fn schema_inspect_schema() -> JsonObject {
    serde_json::from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "domain": { "type": "string", "enum": ["blueprint", "material", "pcg", "widget"] },
            "tool": { "type": "string", "minLength": 1 },
            "operation": {
                "type": "string",
                "description": "Optional operation or command name within the tool."
            },
            "include": {
                "type": "array",
                "items": {
                    "type": "string",
                    "enum": ["summary", "input", "operation", "examples", "errors", "notes", "output"]
                },
                "default": ["summary", "operation"]
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
    if domain != "blueprint" && domain != "material" && domain != "pcg" && domain != "widget" {
        return CallToolResult::structured_error(serde_json::json!({
            "isError": true,
            "code": "UNKNOWN_DOMAIN",
            "message": format!("Unknown schema domain: {domain}"),
            "availableDomains": ["blueprint", "material", "pcg", "widget"],
            "retryable": false
        }));
    }

    let Some(tool) = args.get("tool").and_then(|value| value.as_str()) else {
        return invalid_argument_result("schema.inspect requires tool.");
    };
    let includes = match parse_schema_inspect_includes(args) {
        Ok(value) => value,
        Err(error) => return error,
    };

    manifest_schema_inspect(
        domain,
        tool,
        args.get("operation").and_then(|value| value.as_str()),
        &includes,
    )
}

fn manifest_schema_inspect(
    domain: &str,
    tool_name: &str,
    operation: Option<&str>,
    includes: &[String],
) -> CallToolResult {
    let manifest = match parse_tool_manifest() {
        Ok(value) => value,
        Err(message) => {
            return CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "INVALID_TOOL_MANIFEST",
                "message": message,
                "retryable": false
            }))
        }
    };
    let Some(tools) = manifest.get("tools").and_then(|value| value.as_array()) else {
        return CallToolResult::structured_error(serde_json::json!({
            "isError": true,
            "code": "INVALID_TOOL_MANIFEST",
            "message": "manifest.tools must be an array.",
            "retryable": false
        }));
    };
    let available_tools = tools
        .iter()
        .filter(|tool| manifest_tool_matches_domain(tool, domain))
        .filter_map(|tool| tool.get("name").and_then(|value| value.as_str()))
        .collect::<Vec<_>>();
    let Some(tool_value) = tools.iter().find(|tool| {
        tool.get("name").and_then(|value| value.as_str()) == Some(tool_name)
            && manifest_tool_matches_domain(tool, domain)
    }) else {
        return CallToolResult::structured_error(serde_json::json!({
            "isError": true,
            "code": "UNKNOWN_TOOL",
            "message": format!("Unknown schema tool for domain {domain}: {tool_name}"),
            "availableTools": available_tools,
            "retryable": false
        }));
    };
    let schema_inspect = tool_value
        .get("schemaInspect")
        .and_then(|value| value.as_object());

    if operation.is_none() && schema_inspect.is_none() {
        return CallToolResult::structured(manifest_tool_schema_payload(
            domain, tool_name, tool_value, includes,
        ));
    }

    let Some(schema_inspect) = schema_inspect else {
        return CallToolResult::structured_error(serde_json::json!({
            "isError": true,
            "code": "OPERATION_SCHEMA_UNAVAILABLE",
            "message": format!("{tool_name} does not expose operation schemas."),
            "retryable": false
        }));
    };
    let Some(operations) = schema_inspect
        .get("operations")
        .and_then(|value| value.as_array())
    else {
        return CallToolResult::structured_error(serde_json::json!({
            "isError": true,
            "code": "INVALID_TOOL_MANIFEST",
            "message": format!("{tool_name}.schemaInspect.operations must be an array."),
            "retryable": false
        }));
    };

    if let Some(operation_name) = operation {
        let available_operations = operations
            .iter()
            .filter_map(|operation| operation.get("name").and_then(|value| value.as_str()))
            .collect::<Vec<_>>();
        let Some(operation_value) = operations.iter().find(|operation| {
            operation.get("name").and_then(|value| value.as_str()) == Some(operation_name)
        }) else {
            return CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "UNKNOWN_OPERATION",
                "message": format!("Unknown operation for {tool_name}: {operation_name}"),
                "availableOperations": available_operations,
                "retryable": false
            }));
        };
        let mut payload = manifest_operation_payload(
            domain,
            tool_name,
            operation_name,
            operation_value,
            includes,
        );
        append_tool_input_schema(&mut payload, tool_value, includes);
        append_tool_output_schema(&mut payload, tool_value, includes);
        return CallToolResult::structured(payload);
    }

    let mut payload = serde_json::json!({
        "domain": domain,
        "tool": tool_name,
        "operations": operations.iter().map(manifest_operation_index_entry).collect::<Vec<_>>(),
        "source": schema_inspect.get("source").cloned().unwrap_or_else(|| serde_json::json!({}))
    });
    append_tool_input_schema(&mut payload, tool_value, includes);
    append_tool_output_schema(&mut payload, tool_value, includes);
    CallToolResult::structured(payload)
}

fn manifest_tool_matches_domain(tool: &serde_json::Value, domain: &str) -> bool {
    if tool
        .get("schemaInspect")
        .and_then(|value| value.get("domain"))
        .and_then(|value| value.as_str())
        == Some(domain)
    {
        return true;
    }
    tool.get("name")
        .and_then(|value| value.as_str())
        .is_some_and(|name| {
            name == domain
                || name.starts_with(&format!("{domain}."))
                || name.starts_with(&format!("{domain}_"))
        })
}

fn manifest_tool_schema_payload(
    domain: &str,
    tool_name: &str,
    tool: &serde_json::Value,
    includes: &[String],
) -> serde_json::Value {
    let mut payload = serde_json::json!({
        "domain": domain,
        "tool": tool_name,
        "operations": []
    });
    append_tool_input_schema(&mut payload, tool, includes);
    append_tool_output_schema(&mut payload, tool, includes);
    payload
}

fn append_tool_input_schema(
    payload: &mut serde_json::Value,
    tool: &serde_json::Value,
    includes: &[String],
) {
    if !schema_include_requested(includes, "input") {
        return;
    }
    if let Some(object) = payload.as_object_mut() {
        if let Some(input_schema) = tool.get("inputSchema") {
            object.insert("hasInputSchema".to_string(), serde_json::json!(true));
            object.insert("inputSchema".to_string(), input_schema.clone());
        } else {
            object.insert("hasInputSchema".to_string(), serde_json::json!(false));
        }
    }
}

fn append_tool_output_schema(
    payload: &mut serde_json::Value,
    tool: &serde_json::Value,
    includes: &[String],
) {
    if !schema_include_requested(includes, "output") {
        return;
    }
    if let Some(object) = payload.as_object_mut() {
        if let Some(output_schema) = tool.get("outputSchema") {
            object.insert("hasOutputSchema".to_string(), serde_json::json!(true));
            object.insert("outputSchema".to_string(), output_schema.clone());
        } else {
            object.insert("hasOutputSchema".to_string(), serde_json::json!(false));
        }
    }
}

fn parse_tool_manifest() -> Result<serde_json::Value, String> {
    let manifest: serde_json::Value = serde_json::from_str(TOOL_MANIFEST_JSON)
        .map_err(|error| format!("manifest JSON parse failed: {error}"))?;
    if manifest.get("product").and_then(|value| value.as_str()) != Some("loomle") {
        return Err("manifest product must be loomle".to_string());
    }
    Ok(manifest)
}

fn manifest_operation_index_entry(operation: &serde_json::Value) -> serde_json::Value {
    let mut entry = serde_json::Map::new();
    for field in ["name", "category", "summary"] {
        if let Some(value) = operation.get(field) {
            entry.insert(field.to_string(), value.clone());
        }
    }
    serde_json::Value::Object(entry)
}

fn manifest_operation_payload(
    domain: &str,
    tool_name: &str,
    operation_name: &str,
    operation: &serde_json::Value,
    includes: &[String],
) -> serde_json::Value {
    let mut payload = serde_json::Map::new();
    payload.insert("domain".to_string(), serde_json::json!(domain));
    payload.insert("tool".to_string(), serde_json::json!(tool_name));
    payload.insert("operation".to_string(), serde_json::json!(operation_name));
    if let Some(category) = operation.get("category") {
        payload.insert("category".to_string(), category.clone());
    }
    for field in includes {
        if field == "operation" {
            if let Some(value) = operation.get("schema") {
                payload.insert("operationSchema".to_string(), value.clone());
            }
        } else if let Some(value) = operation.get(field) {
            payload.insert(field.clone(), value.clone());
        } else if matches!(field.as_str(), "examples" | "errors" | "notes") {
            payload.insert(field.clone(), serde_json::json!([]));
        }
    }
    serde_json::Value::Object(payload)
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

fn call_widget_tree_edit_schema_inspect(args: &JsonObject, includes: &[String]) -> CallToolResult {
    if let Some(operation) = args.get("operation").and_then(|value| value.as_str()) {
        let Some(payload) = widget_tree_edit_operation_schema(operation, includes) else {
            return CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "UNKNOWN_OPERATION",
                "message": format!("Unknown operation for widget.tree.edit: {operation}"),
                "availableOperations": widget_tree_edit_operation_names(),
                "retryable": false
            }));
        };
        return CallToolResult::structured(payload);
    }

    CallToolResult::structured(serde_json::json!({
        "domain": "widget",
        "tool": "widget.tree.edit",
        "operations": widget_tree_edit_operation_index(),
        "source": {
            "document": "UE UMG WidgetTree and Widget Palette",
            "section": "WidgetTree edit command set"
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

fn call_node_edit_schema_inspect(args: &JsonObject, includes: &[String]) -> CallToolResult {
    if let Some(operation) = args.get("operation").and_then(|value| value.as_str()) {
        let Some(payload) = blueprint_node_edit_operation_schema(operation, includes) else {
            return CallToolResult::structured_error(serde_json::json!({
                "isError": true,
                "code": "UNKNOWN_OPERATION",
                "message": format!("Unknown operation for blueprint.node.edit: {operation}"),
                "availableOperations": blueprint_node_edit_operation_names(),
                "retryable": false
            }));
        };
        return CallToolResult::structured(payload);
    }

    CallToolResult::structured(serde_json::json!({
        "domain": "blueprint",
        "tool": "blueprint.node.edit",
        "operations": blueprint_node_edit_operation_index(),
        "source": {
            "document": "UE K2 node local pin actions",
            "section": "Switch, Sequence, MakeContainer, Select, FormatText"
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
    let allowed = [
        "summary",
        "input",
        "operation",
        "examples",
        "errors",
        "notes",
        "output",
    ];
    let Some(include_value) = args.get("include") else {
        return Ok(vec!["summary".to_string(), "operation".to_string()]);
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

fn blueprint_node_edit_operation_names() -> Vec<&'static str> {
    vec![
        "addPin",
        "removePin",
        "insertPin",
        "renamePin",
        "movePin",
        "restorePins",
        "setDelegateFunction",
    ]
}

fn blueprint_node_edit_operation_index() -> Vec<serde_json::Value> {
    blueprint_node_edit_operation_names()
        .into_iter()
        .map(|name| {
            serde_json::json!({
                "name": name,
                "category": node_edit_operation_category(name),
                "summary": node_edit_operation_summary(name)
            })
        })
        .collect()
}

fn node_edit_operation_category(operation: &str) -> &'static str {
    match operation {
        "setDelegateFunction" => "node-local-delegate-state",
        _ => "node-local-pin-structure",
    }
}

fn node_edit_operation_summary(operation: &str) -> &'static str {
    match operation {
        "addPin" => "Add one node-local pin/case/argument through the node's UE-native add-pin action.",
        "removePin" => "Remove or hide one removable node-local pin/case/argument by current pin name.",
        "insertPin" => "Insert one execution pin before or after a Sequence/MultiGate execution output pin.",
        "renamePin" => "Rename a node-owned case or argument pin when the UE node exposes a stable backing name list.",
        "movePin" => "Move a node-owned pin/argument before or after another node-owned pin when UE exposes ordering.",
        "restorePins" => "Restore node-local pins hidden by UE node field-visibility actions.",
        "setDelegateFunction" => "Set the target function/event selected by a Create Event delegate node.",
        _ => "Edit node-local Blueprint pin structure.",
    }
}

fn blueprint_node_edit_operation_schema(
    operation: &str,
    includes: &[String],
) -> Option<serde_json::Value> {
    if !blueprint_node_edit_operation_names()
        .iter()
        .any(|name| name == &operation)
    {
        return None;
    }

    let schema = serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": {"type":"string","minLength":1},
            "graph": blueprint_graph_ref_schema_fragment(),
            "node": {
                "type":"object",
                "properties":{"id":{"type":"string","minLength":1}},
                "required":["id"],
                "additionalProperties":false
            },
            "operation": {"const": operation},
            "args": node_edit_args_schema(operation),
            "dryRun": {"type":"boolean","default":false},
            "returnDiff": {"type":"boolean","default":false},
            "returnDiagnostics": {"type":"boolean","default":true},
            "expectedRevision": {"type":"string"}
        },
        "required": ["assetPath", "graph", "node", "operation", "args"],
        "additionalProperties": false
    });

    let mut payload = serde_json::json!({
        "domain": "blueprint",
        "tool": "blueprint.node.edit",
        "operation": operation,
        "category": node_edit_operation_category(operation),
        "source": {
            "document": "UE K2 node local pin actions",
            "section": operation
        }
    });
    let payload_object = payload.as_object_mut()?;
    if schema_include_requested(includes, "summary") {
        payload_object.insert(
            "summary".to_string(),
            serde_json::json!(node_edit_operation_summary(operation)),
        );
    }
    if schema_include_requested(includes, "operation") {
        payload_object.insert("operationSchema".to_string(), schema);
    }
    if schema_include_requested(includes, "examples") {
        payload_object.insert(
            "examples".to_string(),
            serde_json::json!([node_edit_example(operation)]),
        );
    }
    if schema_include_requested(includes, "errors") {
        payload_object.insert(
            "errors".to_string(),
            serde_json::json!([
                "INVALID_ARGUMENT",
                "ASSET_NOT_FOUND",
                "GRAPH_NOT_FOUND",
                "NODE_NOT_FOUND",
                "PIN_NOT_FOUND",
                "UNSUPPORTED_NODE_OPERATION",
                "UNSUPPORTED_PIN_OPERATION",
                "DELEGATE_SIGNATURE_UNAVAILABLE",
                "DELEGATE_SCOPE_UNAVAILABLE",
                "DELEGATE_FUNCTION_NOT_FOUND",
                "DELEGATE_FUNCTION_SIGNATURE_MISMATCH",
                "DELEGATE_FUNCTION_NOT_BINDABLE"
            ]),
        );
    }
    if schema_include_requested(includes, "notes") {
        payload_object.insert(
            "notes".to_string(),
            serde_json::json!([
                "Call blueprint.node.inspect first. It returns editCapabilities and the current pin names accepted by these operations.",
                "Use blueprint.graph.edit for graph topology, links, placement, pin defaults, node creation, and node deletion.",
                "Use blueprint.member.edit for EditablePinBase function, macro, event, and dispatcher signature changes; those are Blueprint members, not node-local K2 add-pin actions.",
                "UK2Node_Select removePin follows UE's RemoveOptionPinToNode behavior: it removes the last removable option, not an arbitrary named option.",
                "UK2Node_SetFieldsInStruct removePin hides struct field pins; restorePins restores all hidden struct field pins.",
                "UK2Node_CreateDelegate setDelegateFunction follows the Create Event node's SetFunction plus HandleAnyChange path and validates against the connected delegate signature."
            ]),
        );
    }
    Some(payload)
}

fn blueprint_graph_ref_schema_fragment() -> serde_json::Value {
    serde_json::json!({
        "type":"object",
        "properties":{
            "id":{"type":"string","minLength":1},
            "name":{"type":"string","minLength":1}
        },
        "additionalProperties": false,
        "anyOf":[{"required":["id"]},{"required":["name"]}]
    })
}

fn node_edit_args_schema(operation: &str) -> serde_json::Value {
    match operation {
        "addPin" => serde_json::json!({
            "type":"object",
            "properties":{
                "role":{
                    "type":"string",
                    "enum":["case","exec","input","pair","option","argument"],
                    "description":"Semantic role from blueprint.node.inspect editCapabilities. Examples: case for Switch, exec for Sequence/MultiGate, pair for MakeMap, argument for FormatText."
                },
                "name":{"type":"string","minLength":1,"description":"Optional desired name when the node supports named cases or arguments, such as Switch on Name/String or FormatText."}
            },
            "required":["role"],
            "additionalProperties":false
        }),
        "removePin" => serde_json::json!({
            "type":"object",
            "properties":{
                "pin":{"type":"string","minLength":1,"description":"Current pin name from blueprint.node.inspect node.pins/editState. Optional only for Select, where UE removes the last option."},
                "mode":{"type":"string","enum":["pin","otherPins"],"default":"pin","description":"For SetFieldsInStruct only: pin hides the selected field; otherPins hides all other visible fields."},
                "target":{
                    "type":"object",
                    "properties":{"pin":{"type":"string","minLength":1}},
                    "required":["pin"],
                    "additionalProperties":false
                }
            },
            "additionalProperties":false
        }),
        "insertPin" => serde_json::json!({
            "type":"object",
            "properties":{
                "target":{
                    "type":"object",
                    "description":"Existing execution output pin used as the insertion anchor.",
                    "properties":{"pin":{"type":"string","minLength":1}},
                    "required":["pin"],
                    "additionalProperties":false
                },
                "position":{"type":"string","enum":["before","after"],"default":"after"}
            },
            "required":["target"],
            "additionalProperties":false
        }),
        "renamePin" => serde_json::json!({
            "type":"object",
            "properties":{
                "pin":{"type":"string","minLength":1,"description":"Current pin name from blueprint.node.inspect node.pins/editState."},
                "name":{"type":"string","minLength":1,"description":"New case or argument name."}
            },
            "required":["pin","name"],
            "additionalProperties":false
        }),
        "movePin" => serde_json::json!({
            "type":"object",
            "properties":{
                "pin":{"type":"string","minLength":1,"description":"Current pin name from blueprint.node.inspect node.pins/editState."},
                "target":{
                    "type":"object",
                    "properties":{"pin":{"type":"string","minLength":1}},
                    "required":["pin"],
                    "additionalProperties":false
                },
                "position":{"type":"string","enum":["before","after"],"default":"after"}
            },
            "required":["pin","target"],
            "additionalProperties":false
        }),
        "restorePins" => serde_json::json!({
            "type":"object",
            "properties":{
                "scope":{"type":"string","enum":["all"],"default":"all"}
            },
            "additionalProperties":false
        }),
        "setDelegateFunction" => serde_json::json!({
            "type":"object",
            "properties":{
                "functionName":{"type":"string","minLength":1,"description":"Existing function or event name on the Create Event node's object scope."}
            },
            "required":["functionName"],
            "additionalProperties":false
        }),
        _ => serde_json::json!({"type":"object","additionalProperties":true}),
    }
}

fn node_edit_example(operation: &str) -> serde_json::Value {
    match operation {
        "addPin" => serde_json::json!({
            "assetPath": "/Game/BP_Demo",
            "graph": {"name": "EventGraph"},
            "node": {"id": "switch-name-guid"},
            "operation": "addPin",
            "args": {"role": "case", "name": "Paused"}
        }),
        "removePin" => serde_json::json!({
            "assetPath": "/Game/BP_Demo",
            "graph": {"name": "EventGraph"},
            "node": {"id": "sequence-guid"},
            "operation": "removePin",
            "args": {"pin": "Then_1"}
        }),
        "insertPin" => serde_json::json!({
            "assetPath": "/Game/BP_Demo",
            "graph": {"name": "EventGraph"},
            "node": {"id": "sequence-guid"},
            "operation": "insertPin",
            "args": {"target": {"pin": "Then_0"}, "position": "after"}
        }),
        "renamePin" => serde_json::json!({
            "assetPath": "/Game/BP_Demo",
            "graph": {"name": "EventGraph"},
            "node": {"id": "format-text-guid"},
            "operation": "renamePin",
            "args": {"pin": "Arg0", "name": "PlayerName"}
        }),
        "movePin" => serde_json::json!({
            "assetPath": "/Game/BP_Demo",
            "graph": {"name": "EventGraph"},
            "node": {"id": "format-text-guid"},
            "operation": "movePin",
            "args": {"pin": "Score", "target": {"pin": "PlayerName"}, "position": "after"}
        }),
        "restorePins" => serde_json::json!({
            "assetPath": "/Game/BP_Demo",
            "graph": {"name": "EventGraph"},
            "node": {"id": "set-members-guid"},
            "operation": "restorePins",
            "args": {"scope": "all"}
        }),
        "setDelegateFunction" => serde_json::json!({
            "assetPath": "/Game/UI/WBP_Menu",
            "graph": {"name": "OnBuildCards"},
            "node": {"id": "create-event-guid"},
            "operation": "setDelegateFunction",
            "args": {"functionName": "HandleWorldCardClicked"}
        }),
        _ => serde_json::json!({}),
    }
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
        "function.override",
        "function.updateSignature",
        "function.rename",
        "function.setFlags",
        "function.delete",
        "macro.create",
        "macro.updateSignature",
        "macro.rename",
        "macro.delete",
        "dispatcher.create",
        "dispatcher.updateSignature",
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
        ("function", "override") => {
            "Create or confirm an inherited Blueprint override function graph."
        }
        ("function", "updateSignature") => "Replace one Blueprint function signature.",
        ("function", "rename") => "Rename one Blueprint function.",
        ("function", "setFlags") => "Update Blueprint function flags and metadata.",
        ("function", "delete") => "Delete one Blueprint function.",
        ("macro", "create") => "Create one Blueprint macro graph and signature.",
        ("macro", "updateSignature") => "Replace one Blueprint macro signature.",
        ("macro", "rename") => "Rename one Blueprint macro.",
        ("macro", "delete") => "Delete one Blueprint macro.",
        ("dispatcher", "create") => "Create one Blueprint event dispatcher.",
        ("dispatcher", "updateSignature") => "Replace one Blueprint event dispatcher signature.",
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
            "dryRun": {"type":"boolean","default":false}
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
            "document": "docs/blueprint/member-edit.md",
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
    if schema_include_requested(includes, "operation") {
        payload_object.insert("operationSchema".to_string(), schema);
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
        ("function", "override") => serde_json::json!({
            "type":"object",
            "properties":{
                "functionName":{"type":"string","minLength":1},
                "name":{"type":"string","minLength":1},
                "ownerClassPath":{"type":"string","minLength":1},
                "classPath":{"type":"string","minLength":1}
            },
            "anyOf":[{"required":["functionName"]},{"required":["name"]}],
            "additionalProperties":false
        }),
        ("function", "updateSignature") => function_like_signature_schema("functionName"),
        ("function", "rename") => name_schema("functionName"),
        ("function", "setFlags") => object_with_required_name("functionName"),
        ("function", "delete") => single_name_schema("functionName"),
        ("macro", "create") => function_like_create_schema("macroName"),
        ("macro", "updateSignature") => function_like_signature_schema("macroName"),
        ("macro", "rename") => name_schema("macroName"),
        ("macro", "delete") => single_name_schema("macroName"),
        ("dispatcher", "create") => function_like_create_schema("dispatcherName"),
        ("dispatcher", "updateSignature") => function_like_signature_schema("dispatcherName"),
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

fn function_like_signature_schema(name_field: &str) -> serde_json::Value {
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
    serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":[name_field],
        "additionalProperties":false
    })
}

fn member_edit_example(member_kind: &str, operation: &str) -> serde_json::Value {
    match (member_kind, operation) {
        ("variable", "create") => {
            serde_json::json!({"assetPath":"/Game/BP_Test","memberKind":"variable","operation":"create","args":{"variableName":"bIsReady","type":{"category":"bool"},"defaultValue":"false"}})
        }
        ("function", "override") => {
            serde_json::json!({"assetPath":"/Game/BP_Test","memberKind":"function","operation":"override","args":{"functionName":"GetBodyMesh","ownerClassPath":"/Script/MyModule.MyAvatarBase"}})
        }
        ("function", "updateSignature") => {
            serde_json::json!({"assetPath":"/Game/BP_Test","memberKind":"function","operation":"updateSignature","args":{"functionName":"ComputeValue","inputs":[{"name":"bInput","type":{"category":"bool"}}],"outputs":[{"name":"Value","type":{"category":"int"}}]}})
        }
        ("macro", "updateSignature") => {
            serde_json::json!({"assetPath":"/Game/BP_Test","memberKind":"macro","operation":"updateSignature","args":{"macroName":"GuardMacro","inputs":[{"name":"bGate","type":{"category":"bool"}}],"outputs":[{"name":"bPassed","type":{"category":"bool"}}]}})
        }
        ("dispatcher", "updateSignature") => {
            serde_json::json!({"assetPath":"/Game/BP_Test","memberKind":"dispatcher","operation":"updateSignature","args":{"dispatcherName":"OnReady","inputs":[{"name":"bReady","type":{"category":"bool"}}]}})
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
        serde_json::json!({"name":"addFromPalette","category":"core","summary":"Execute one selected blueprint.graph.palette entry."}),
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
            "Execute one selected blueprint.graph.palette entry.",
            serde_json::json!({
                "type":"object",
                "properties":{
                    "kind":{"const":"addFromPalette"},
                    "entry":{"type":"object","properties":{"id":{"type":"string","minLength":1}},"required":["id"],"additionalProperties":true},
                    "position":{"$ref":"#/$defs/position"},
                    "alias":{"type":"string","minLength":1},
                    "fromPins":{"type":"array","items":{"$ref":"#/$defs/pinRef"}},
                    "eventName":{
                        "type":"string",
                        "minLength":1,
                        "description":"Optional custom event name for palette actions that create an attached UK2Node_CustomEvent, such as Assign Delegate."
                    },
                    "contextSensitive":{"type":"boolean"},
                    "context":{
                        "type":"object",
                        "description":"Optional UE action-menu context. Usually inherited from the selected palette entry.",
                        "properties":{
                            "selectedObjects":{
                                "type":"array",
                                "items":{
                                    "type":"object",
                                    "properties":{
                                        "kind":{"type":"string","enum":["component_property"]},
                                        "name":{"type":"string","minLength":1}
                                    },
                                    "required":["kind","name"],
                                    "additionalProperties":false
                                }
                            },
                            "component":{
                                "type":"object",
                                "properties":{"name":{"type":"string","minLength":1}},
                                "required":["name"],
                                "additionalProperties":false
                            }
                        },
                        "additionalProperties":false
                    }
                },
                "required":["kind","entry"],
                "additionalProperties":false
            }),
            vec![
                serde_json::json!({"kind":"addFromPalette","entry":{"id":"palette:..."},"position":{"x":400,"y":200},"alias":"branch"}),
            ],
            vec!["PALETTE_ENTRY_NOT_EXECUTABLE"],
            vec!["entry should be the full object returned by blueprint.graph.palette."],
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
                serde_json::json!({"kind":"setPinDefault","target":{"node":{"alias":"createCard"},"pin":"Class"},"value":{"object":"/Game/UI/WBP_OasiumWorldCard.WBP_OasiumWorldCard_C"}}),
                serde_json::json!({"kind":"setPinDefault","target":{"node":{"alias":"createCard"},"pin":"Class"},"value":{"object":"/Game/UI/WBP_OasiumWorldCard"}}),
            ],
            vec![
                "PIN_NOT_FOUND",
                "PIN_DEFAULT_NOT_EDITABLE",
                "PIN_DEFAULT_REQUIRES_UNLINKED_PIN",
                "PIN_DEFAULT_OBJECT_NOT_FOUND",
                "PIN_DEFAULT_OBJECT_INVALID_FOR_PIN",
                "PIN_DEFAULT_CLASS_REQUIRED",
                "PIN_DEFAULT_CLASS_NOT_CHILD_OF_BASE",
            ],
            vec![
                "setPinDefault must not implicitly break links.",
                "For object/class pins, pass value.object. Blueprint asset paths resolve to their generated class when the target pin requires a class.",
                "Class pin updates trigger UE node-specific PinDefaultValueChanged behavior so construct/spawn nodes rebuild dependent pins.",
            ],
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
    if schema_include_requested(includes, "operation") {
        payload_object.insert(
            "operationSchema".to_string(),
            add_graph_edit_schema_defs(schema),
        );
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

fn widget_tree_edit_operation_names() -> Vec<&'static str> {
    vec![
        "addFromPalette",
        "removeWidget",
        "renameWidget",
        "reparentWidget",
        "setIsVariable",
    ]
}

fn widget_tree_edit_operation_index() -> Vec<serde_json::Value> {
    vec![
        serde_json::json!({"name":"addFromPalette","category":"core","summary":"Create one UMG widget from a selected widget.palette entry."}),
        serde_json::json!({"name":"removeWidget","category":"core","summary":"Remove one widget from the WidgetTree."}),
        serde_json::json!({"name":"renameWidget","category":"core","summary":"Rename one widget in the WidgetTree."}),
        serde_json::json!({"name":"reparentWidget","category":"core","summary":"Move one widget under a different parent widget."}),
        serde_json::json!({"name":"setIsVariable","category":"core","summary":"Set whether one WidgetTree widget is exposed as a Blueprint variable."}),
    ]
}

fn add_widget_tree_edit_schema_defs(mut schema: serde_json::Value) -> serde_json::Value {
    if let Some(object) = schema.as_object_mut() {
        object.insert(
            "$defs".to_string(),
            serde_json::json!({
                "widgetRef": {
                    "type": "object",
                    "properties": {
                        "name": { "type": "string", "minLength": 1 }
                    },
                    "required": ["name"],
                    "additionalProperties": false
                }
            }),
        );
    }
    schema
}

fn widget_tree_edit_operation_schema(
    operation: &str,
    includes: &[String],
) -> Option<serde_json::Value> {
    let (category, summary, schema, examples, errors, notes) = match operation {
        "addFromPalette" => (
            "core",
            "Create one UMG widget from a selected widget.palette entry.",
            serde_json::json!({
                "type":"object",
                "properties":{
                    "kind":{"const":"addFromPalette"},
                    "entry":{"type":"object","properties":{"id":{"type":"string","minLength":1},"kind":{"type":"string"},"payload":{"type":"object","properties":{"widgetClass":{"type":"string","minLength":1}},"required":["widgetClass"],"additionalProperties":true},"executable":{"type":"boolean"}},"required":["id","payload"],"additionalProperties":true},
                    "name":{"type":"string","minLength":1},
                    "parent":{"oneOf":[{"type":"string","minLength":1},{"$ref":"#/$defs/widgetRef"}]},
                    "parentName":{"type":"string","minLength":1},
                    "slot":{"type":"object","additionalProperties":true},
                    "isVariable":{"type":"boolean"}
                },
                "required":["kind","entry","name"],
                "additionalProperties":false
            }),
            vec![serde_json::json!({"kind":"addFromPalette","entry":{"id":"widget.palette:...","kind":"native","payload":{"widgetClass":"/Script/UMG.TextBlock"}},"name":"TitleText","parent":{"name":"RootCanvas"},"isVariable":true})],
            vec!["PALETTE_ENTRY_NOT_EXECUTABLE", "INVALID_PALETTE_ENTRY"],
            vec!["Use widget.palette first and pass the full selected entry. Do not guess widget classes in public calls.", "parent may be omitted only when creating the root widget."],
        ),
        "removeWidget" => (
            "core",
            "Remove one widget from the WidgetTree.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"removeWidget"},"target":{"$ref":"#/$defs/widgetRef"},"name":{"type":"string","minLength":1}},"required":["kind"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"removeWidget","target":{"name":"TitleText"}})],
            vec!["WIDGET_NOT_FOUND"],
            vec!["Removing a panel also removes its child widgets."],
        ),
        "renameWidget" => (
            "core",
            "Rename one widget in the WidgetTree.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"renameWidget"},"target":{"$ref":"#/$defs/widgetRef"},"oldName":{"type":"string","minLength":1},"name":{"type":"string","minLength":1,"description":"New widget name."},"newName":{"type":"string","minLength":1}},"required":["kind"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"renameWidget","target":{"name":"WorldSelect_Card0_Button"},"name":"CardButton"})],
            vec!["WIDGET_NOT_FOUND", "WIDGET_ALREADY_EXISTS", "RENAME_WIDGET_FAILED"],
            vec!["Renaming preserves the widget object, slot, layout, style, properties, and bindings."],
        ),
        "reparentWidget" => (
            "core",
            "Move one widget under a different parent widget.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"reparentWidget"},"target":{"$ref":"#/$defs/widgetRef"},"name":{"type":"string","minLength":1},"newParent":{"oneOf":[{"type":"string","minLength":1},{"$ref":"#/$defs/widgetRef"}]},"slot":{"type":"object","additionalProperties":true}},"required":["kind","newParent"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"reparentWidget","target":{"name":"TitleText"},"newParent":{"name":"ContentBox"}})],
            vec!["WIDGET_NOT_FOUND", "PARENT_NOT_FOUND", "REPARENT_FAILED"],
            vec!["slot values are panel-slot properties and depend on the new parent widget type."],
        ),
        "setIsVariable" => (
            "core",
            "Set whether one WidgetTree widget is exposed as a Blueprint variable.",
            serde_json::json!({"type":"object","properties":{"kind":{"const":"setIsVariable"},"target":{"$ref":"#/$defs/widgetRef"},"name":{"type":"string","minLength":1},"value":{"type":"boolean"}},"required":["kind","value"],"additionalProperties":false}),
            vec![serde_json::json!({"kind":"setIsVariable","target":{"name":"WorldCardGrid"},"value":true})],
            vec!["WIDGET_NOT_FOUND"],
            vec!["This is a WidgetBlueprint structure operation, not a reflected widget property edit."],
        ),
        _ => return None,
    };

    let mut payload = serde_json::json!({
        "domain": "widget",
        "tool": "widget.tree.edit",
        "operation": operation,
        "category": category,
        "source": {
            "document": "UE UMG WidgetTree and Widget Palette",
            "section": operation
        }
    });
    let payload_object = payload.as_object_mut()?;
    if schema_include_requested(includes, "summary") {
        payload_object.insert("summary".to_string(), serde_json::json!(summary));
    }
    if schema_include_requested(includes, "operation") {
        payload_object.insert(
            "operationSchema".to_string(),
            add_widget_tree_edit_schema_defs(schema),
        );
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
    if schema_include_requested(includes, "operation") {
        payload_object.insert("operationSchema".to_string(), schema);
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
    if schema_include_requested(includes, "operation") {
        payload_object.insert(
            "operationSchema".to_string(),
            add_graph_edit_schema_defs(schema),
        );
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
            vec!["Material root inputs are addressed as node id __material_root__ with the root pin name returned by material.graph.inspect."],
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
    if schema_include_requested(includes, "operation") {
        payload_object.insert(
            "operationSchema".to_string(),
            add_graph_edit_schema_defs(schema),
        );
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
