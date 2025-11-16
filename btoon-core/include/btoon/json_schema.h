/**
 * @file json_schema.h
 * @brief JSON Schema compatibility for BTOON
 * 
 * Provides bidirectional conversion between BTOON schemas and JSON Schema,
 * enabling validation and compatibility with JSON Schema tooling.
 */

#ifndef BTOON_JSON_SCHEMA_H
#define BTOON_JSON_SCHEMA_H

#include "btoon/schema.h"
#include "btoon/btoon.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <variant>
#include <memory>

namespace btoon {
namespace json_schema {

/**
 * @brief JSON Schema draft versions
 */
enum class JsonSchemaDraft {
    DRAFT_04,
    DRAFT_06,
    DRAFT_07,
    DRAFT_2019_09,
    DRAFT_2020_12
};

/**
 * @brief JSON Schema type definitions
 */
enum class JsonSchemaType {
    NULL_TYPE,
    BOOLEAN,
    OBJECT,
    ARRAY,
    NUMBER,
    STRING,
    INTEGER
};

/**
 * @brief JSON Schema format specifiers
 */
enum class JsonSchemaFormat {
    NONE,
    // String formats
    DATE_TIME,
    DATE,
    TIME,
    DURATION,
    EMAIL,
    IDN_EMAIL,
    HOSTNAME,
    IDN_HOSTNAME,
    IPV4,
    IPV6,
    URI,
    URI_REFERENCE,
    IRI,
    IRI_REFERENCE,
    UUID,
    JSON_POINTER,
    RELATIVE_JSON_POINTER,
    REGEX,
    // Number formats
    INT32,
    INT64,
    FLOAT,
    DOUBLE,
    // BTOON custom formats
    DECIMAL,
    TIMESTAMP,
    BINARY
};

/**
 * @brief JSON Schema property definition
 */
struct JsonSchemaProperty {
    std::vector<JsonSchemaType> type;  // Can be multiple types
    std::optional<std::string> description;
    std::optional<JsonSchemaFormat> format;
    std::optional<Value> default_value;
    std::optional<Value> const_value;
    std::vector<Value> enum_values;
    
    // String constraints
    std::optional<size_t> min_length;
    std::optional<size_t> max_length;
    std::optional<std::string> pattern;
    
    // Number constraints
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<bool> exclusive_minimum;
    std::optional<bool> exclusive_maximum;
    std::optional<double> multiple_of;
    
    // Array constraints
    std::optional<size_t> min_items;
    std::optional<size_t> max_items;
    std::optional<bool> unique_items;
    std::shared_ptr<JsonSchemaProperty> items;  // Array item schema
    
    // Object constraints
    std::unordered_map<std::string, JsonSchemaProperty> properties;
    std::vector<std::string> required;
    std::optional<size_t> min_properties;
    std::optional<size_t> max_properties;
    std::shared_ptr<JsonSchemaProperty> additional_properties;
    std::unordered_map<std::string, JsonSchemaProperty> pattern_properties;
    
    // Composition
    std::vector<JsonSchemaProperty> all_of;
    std::vector<JsonSchemaProperty> any_of;
    std::vector<JsonSchemaProperty> one_of;
    std::shared_ptr<JsonSchemaProperty> not_schema;
    
    // References
    std::optional<std::string> ref;  // $ref
    std::optional<std::string> id;   // $id
    
    // Conditional
    std::shared_ptr<JsonSchemaProperty> if_schema;
    std::shared_ptr<JsonSchemaProperty> then_schema;
    std::shared_ptr<JsonSchemaProperty> else_schema;
};

/**
 * @brief JSON Schema document
 */
class JsonSchema {
public:
    JsonSchema(JsonSchemaDraft draft = JsonSchemaDraft::DRAFT_2020_12);
    
    // Schema metadata
    void set_id(const std::string& id);
    void set_title(const std::string& title);
    void set_description(const std::string& description);
    void set_version(const std::string& version);
    
    // Schema properties
    void set_root_schema(const JsonSchemaProperty& schema);
    const JsonSchemaProperty& get_root_schema() const;
    
    // Definitions/Components
    void add_definition(const std::string& name, const JsonSchemaProperty& schema);
    const JsonSchemaProperty* get_definition(const std::string& name) const;
    
    // Serialization
    std::string to_json(bool pretty = true) const;
    static JsonSchema from_json(const std::string& json);
    
    // Validation
    bool validate(const Value& data, std::vector<std::string>& errors) const;
    
private:
    JsonSchemaDraft draft_;
    std::string id_;
    std::string title_;
    std::string description_;
    std::string version_;
    JsonSchemaProperty root_schema_;
    std::unordered_map<std::string, JsonSchemaProperty> definitions_;
};

/**
 * @brief Converts BTOON schema to JSON Schema
 */
class BtoonToJsonSchema {
public:
    struct ConversionOptions {
        JsonSchemaDraft target_draft = JsonSchemaDraft::DRAFT_2020_12;
        bool use_definitions = true;        // Use $defs for reusable schemas
        bool strict_validation = false;     // Add strict constraints
        bool include_examples = true;       // Include example values
        bool generate_formats = true;       // Use format specifiers
        bool preserve_btoon_types = true;   // Preserve BTOON-specific types as extensions
    };
    
    BtoonToJsonSchema(const ConversionOptions& options = ConversionOptions{});
    
    /**
     * @brief Convert BTOON schema to JSON Schema
     */
    JsonSchema convert(const Schema& btoon_schema);
    
    /**
     * @brief Generate JSON Schema string from BTOON schema
     */
    std::string generate_json_schema(const Schema& btoon_schema, bool pretty = true);
    
private:
    ConversionOptions options_;
    
    // Type mapping
    JsonSchemaProperty map_btoon_field(const SchemaField& field);
    std::vector<JsonSchemaType> map_btoon_type(const std::string& btoon_type);
    JsonSchemaFormat map_btoon_format(const std::string& btoon_type);
    
    // Constraint mapping
    void apply_constraints(JsonSchemaProperty& prop, const SchemaField& field);
};

/**
 * @brief Converts JSON Schema to BTOON schema
 */
class JsonSchemaToBtoon {
public:
    struct ConversionOptions {
        bool strict_mode = false;           // Fail on unsupported features
        bool simplify_unions = true;        // Simplify anyOf/oneOf to single type
        bool preserve_descriptions = true;  // Keep descriptions in BTOON schema
        bool infer_btoon_types = true;     // Infer BTOON-specific types from formats
    };
    
    JsonSchemaToBtoon(const ConversionOptions& options = ConversionOptions{});
    
    /**
     * @brief Convert JSON Schema to BTOON schema
     */
    Schema convert(const JsonSchema& json_schema);
    
    /**
     * @brief Parse JSON Schema string to BTOON schema
     */
    Schema parse_json_schema(const std::string& json_schema);
    
private:
    ConversionOptions options_;
    
    // Type mapping
    SchemaField map_json_property(const std::string& name, 
                                  const JsonSchemaProperty& prop);
    std::string map_json_type(const std::vector<JsonSchemaType>& types);
    
    // Handle complex schemas
    SchemaField handle_composition(const std::string& name,
                                   const JsonSchemaProperty& prop);
    SchemaField resolve_reference(const std::string& ref,
                                  const JsonSchema& schema);
};

/**
 * @brief JSON Schema validator for BTOON data
 */
class JsonSchemaValidator {
public:
    JsonSchemaValidator(const JsonSchema& schema);
    
    /**
     * @brief Validate BTOON value against JSON Schema
     */
    bool validate(const Value& value, std::vector<std::string>& errors) const;
    
    /**
     * @brief Validate with detailed error reporting
     */
    struct ValidationError {
        std::string path;
        std::string message;
        std::string schema_path;
        Value invalid_value;
    };
    
    bool validate_detailed(const Value& value, 
                          std::vector<ValidationError>& errors) const;
    
private:
    JsonSchema schema_;
    
    bool validate_property(const Value& value,
                          const JsonSchemaProperty& prop,
                          const std::string& path,
                          std::vector<ValidationError>& errors) const;
    
    bool validate_type(const Value& value, 
                      const std::vector<JsonSchemaType>& types) const;
    
    bool validate_constraints(const Value& value,
                            const JsonSchemaProperty& prop,
                            const std::string& path,
                            std::vector<ValidationError>& errors) const;
};

/**
 * @brief OpenAPI/Swagger integration
 */
class OpenApiIntegration {
public:
    /**
     * @brief Generate OpenAPI schema from BTOON schema
     */
    static std::string generate_openapi_schema(const Schema& btoon_schema,
                                              const std::string& api_version = "3.0.0");
    
    /**
     * @brief Convert OpenAPI schema to BTOON schema
     */
    static Schema from_openapi_schema(const std::string& openapi_json);
    
    /**
     * @brief Generate OpenAPI paths for CRUD operations
     */
    static std::string generate_crud_paths(const Schema& btoon_schema,
                                          const std::string& base_path = "/api");
};

/**
 * @brief JSON Hyper-Schema support
 */
class JsonHyperSchema {
public:
    struct Link {
        std::string rel;
        std::string href;
        std::string method;
        JsonSchemaProperty target_schema;
        JsonSchemaProperty submission_schema;
    };
    
    /**
     * @brief Add hypermedia links to schema
     */
    static void add_links(JsonSchema& schema, const std::vector<Link>& links);
    
    /**
     * @brief Generate HAL links
     */
    static std::vector<Link> generate_hal_links(const Schema& btoon_schema);
    
    /**
     * @brief Generate JSON-LD context
     */
    static std::string generate_json_ld_context(const Schema& btoon_schema);
};

// ============= Utility Functions =============

/**
 * @brief Generate JSON Schema from BTOON data by inference
 */
JsonSchema infer_json_schema(const Value& data, 
                            const std::string& title = "Inferred Schema");

/**
 * @brief Merge multiple JSON Schemas
 */
JsonSchema merge_schemas(const std::vector<JsonSchema>& schemas);

/**
 * @brief Generate TypeScript interfaces from JSON Schema
 */
std::string generate_typescript_from_json_schema(const JsonSchema& schema);

/**
 * @brief Generate validation code for various languages
 */
std::string generate_validator_code(const JsonSchema& schema,
                                   const std::string& language = "javascript");

/**
 * @brief Check if BTOON schema is compatible with JSON Schema
 */
bool is_json_schema_compatible(const Schema& btoon_schema,
                              std::vector<std::string>& incompatibilities);

} // namespace json_schema
} // namespace btoon

#endif // BTOON_JSON_SCHEMA_H
