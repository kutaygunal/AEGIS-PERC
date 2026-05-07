#!/usr/bin/env python3
"""Validate all synthetic sample designs against the JSON schema.

Usage:
    python scripts/validate_samples.py

Exit codes:
    0 — all samples valid
    1 — one or more samples invalid or schema unreadable
"""

import json
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
SAMPLES_DIR = PROJECT_ROOT / "data" / "sample_designs"
SCHEMA_PATH = SAMPLES_DIR / "schema.json"

SAMPLE_FILES = [
    "inverter.json",
    "nand2.json",
    "ring_oscillator.json",
]

# ---------------------------------------------------------------------------
# Minimal built-in validator (no jsonschema required)
# ---------------------------------------------------------------------------

class ValidationError(Exception):
    pass


def validate_type(value, expected, path):
    if expected == "string" and isinstance(value, str):
        return
    if expected == "number" and isinstance(value, (int, float)) and not isinstance(value, bool):
        return
    if expected == "integer" and isinstance(value, int) and not isinstance(value, bool):
        return
    if expected == "boolean" and isinstance(value, bool):
        return
    if expected == "array" and isinstance(value, list):
        return
    if expected == "object" and isinstance(value, dict):
        return
    raise ValidationError(f"{path}: expected {expected}, got {type(value).__name__}")


def validate_pattern(value, pattern, path):
    import re
    if not re.match(pattern, value):
        raise ValidationError(f"{path}: value {value!r} does not match pattern {pattern!r}")


def validate_enum(value, enum_values, path):
    if value not in enum_values:
        raise ValidationError(f"{path}: value {value!r} not in enum {enum_values!r}")


def validate_schema(data, schema, path=""):
    if not isinstance(schema, dict):
        return

    schema_type = schema.get("type")
    if schema_type:
        validate_type(data, schema_type, path)

    if "enum" in schema:
        validate_enum(data, schema["enum"], path)

    if schema_type == "array":
        items_schema = schema.get("items")
        if items_schema:
            for i, item in enumerate(data):
                validate_schema(item, items_schema, f"{path}[{i}]")
        min_items = schema.get("minItems")
        if min_items is not None and len(data) < min_items:
            raise ValidationError(f"{path}: array length {len(data)} < minItems {min_items}")
        max_items = schema.get("maxItems")
        if max_items is not None and len(data) > max_items:
            raise ValidationError(f"{path}: array length {len(data)} > maxItems {max_items}")

    if schema_type == "object":
        required = schema.get("required", [])
        for key in required:
            if key not in data:
                raise ValidationError(f"{path}: missing required key {key!r}")
        props = schema.get("properties", {})
        for key, sub_schema in props.items():
            if key in data:
                validate_schema(data[key], sub_schema, f"{path}.{key}")
        additional = schema.get("additionalProperties")
        if additional is not True:
            for key in data:
                if key not in props and key not in required:
                    if isinstance(additional, dict):
                        validate_schema(data[key], additional, f"{path}.{key}")
                    elif additional is None or additional is False:
                        pass  # allow unknown keys when no explicit restriction

    if schema_type == "string":
        min_len = schema.get("minLength")
        if min_len is not None and len(data) < min_len:
            raise ValidationError(f"{path}: string length {len(data)} < minLength {min_len}")
        pattern = schema.get("pattern")
        if pattern is not None:
            validate_pattern(data, pattern, path)

    if schema_type == "number":
        minimum = schema.get("minimum")
        if minimum is not None and data < minimum:
            raise ValidationError(f"{path}: value {data} < minimum {minimum}")

    # logical combinators: anyOf
    if "anyOf" in schema:
        errors = []
        for sub in schema["anyOf"]:
            try:
                validate_schema(data, sub, path)
                break
            except ValidationError as e:
                errors.append(str(e))
        else:
            raise ValidationError(f"{path}: none of anyOf matched. Errors: {errors}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    if not SCHEMA_PATH.exists():
        print(f"ERROR: schema file not found: {SCHEMA_PATH}", file=sys.stderr)
        return 1

    with open(SCHEMA_PATH, "r", encoding="utf-8") as f:
        schema = json.load(f)

    all_ok = True
    for filename in SAMPLE_FILES:
        path = SAMPLES_DIR / filename
        if not path.exists():
            print(f"MISSING: {path}")
            all_ok = False
            continue

        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)

        try:
            validate_schema(data, schema, "$")
            print(f"PASS    {filename}")
        except ValidationError as exc:
            print(f"FAIL    {filename}  –  {exc}")
            all_ok = False
        except Exception as exc:
            print(f"ERROR   {filename}  –  {exc}")
            all_ok = False

    if all_ok:
        print("All samples validated successfully.")
        return 0
    else:
        print("Validation failed.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
