import { Ajv2020 } from "ajv/dist/2020.js";
import { readFile } from "node:fs/promises";
import type { Diagnostic, LglObject, ObjectResult } from "./index.js";

type SchemaValidator = (value: unknown) => boolean;

let validatorPromise: Promise<SchemaValidator> | undefined;

export async function validateLglObject(
  object: LglObject,
): Promise<Diagnostic | undefined> {
  const validate = await loadValidator();
  return validate(object)
    ? undefined
    : diagnostic(
      "language.invalid_object_shape",
      "Normalized LGL object failed schema validation.",
    );
}

export async function validateObjectResult(
  result: ObjectResult,
): Promise<Diagnostic | undefined> {
  const validate = await loadValidator();
  return validate(result)
    ? undefined
    : diagnostic(
      "language.invalid_result_shape",
      "Adapter result failed schema validation.",
    );
}

function loadValidator(): Promise<SchemaValidator> {
  validatorPromise ??= (async () => {
    const url = new URL("../../schema/lgl-object.schema.json", import.meta.url);
    const schema = JSON.parse(await readFile(url, "utf8"));
    const ajv = new Ajv2020({ allErrors: true, strict: false });
    return ajv.compile(schema) as SchemaValidator;
  })();
  return validatorPromise;
}

function diagnostic(code: string, message: string): Diagnostic {
  return { severity: "error", code, message };
}
