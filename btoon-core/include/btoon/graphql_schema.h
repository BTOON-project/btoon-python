/**
 * @file graphql_schema.h
 * @brief GraphQL schema integration for BTOON
 * 
 * Provides bidirectional conversion between BTOON schemas and GraphQL schemas,
 * enabling seamless integration with GraphQL APIs and tooling.
 */

#ifndef BTOON_GRAPHQL_SCHEMA_H
#define BTOON_GRAPHQL_SCHEMA_H

#include "btoon/schema.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace btoon {
namespace graphql {

/**
 * @brief GraphQL type kinds
 */
enum class GraphQLTypeKind {
    SCALAR,
    OBJECT,
    INTERFACE,
    UNION,
    ENUM,
    INPUT_OBJECT,
    LIST,
    NON_NULL
};

/**
 * @brief GraphQL scalar types
 */
enum class GraphQLScalarType {
    INT,
    FLOAT,
    STRING,
    BOOLEAN,
    ID,
    // Custom scalars for BTOON
    TIMESTAMP,
    DECIMAL,
    BINARY
};

/**
 * @brief GraphQL field definition
 */
struct GraphQLField {
    std::string name;
    std::string type;
    bool is_required;
    bool is_list;
    std::string description;
    std::unordered_map<std::string, std::string> directives;
    
    GraphQLField(const std::string& n, const std::string& t)
        : name(n), type(t), is_required(false), is_list(false) {}
};

/**
 * @brief GraphQL type definition
 */
struct GraphQLType {
    std::string name;
    GraphQLTypeKind kind;
    std::string description;
    std::vector<GraphQLField> fields;
    std::vector<std::string> interfaces;
    std::vector<std::string> possible_types; // For unions
    std::vector<std::string> enum_values;    // For enums
    
    GraphQLType(const std::string& n, GraphQLTypeKind k)
        : name(n), kind(k) {}
};

/**
 * @brief GraphQL schema definition
 */
class GraphQLSchema {
public:
    GraphQLSchema();
    
    // Add types
    void add_type(const GraphQLType& type);
    void add_scalar(const std::string& name, const std::string& description = "");
    
    // Set root types
    void set_query_type(const std::string& type_name);
    void set_mutation_type(const std::string& type_name);
    void set_subscription_type(const std::string& type_name);
    
    // Get types
    const GraphQLType* get_type(const std::string& name) const;
    std::vector<GraphQLType> get_all_types() const;
    
    // Schema generation
    std::string to_sdl() const;  // Schema Definition Language
    std::string to_introspection_json() const;
    
private:
    std::unordered_map<std::string, GraphQLType> types_;
    std::string query_type_;
    std::string mutation_type_;
    std::string subscription_type_;
    
    // Built-in scalars
    void init_built_in_scalars();
};

/**
 * @brief Converts BTOON schema to GraphQL schema
 */
class BtoonToGraphQL {
public:
    struct ConversionOptions {
        bool generate_input_types = true;
        bool generate_mutations = true;
        bool use_custom_scalars = true;
        bool add_pagination = true;
        std::string id_field_name = "id";
    };
    
    BtoonToGraphQL(const ConversionOptions& options = ConversionOptions{});
    
    /**
     * @brief Convert BTOON schema to GraphQL schema
     */
    GraphQLSchema convert(const Schema& btoon_schema);
    
    /**
     * @brief Generate GraphQL SDL from BTOON schema
     */
    std::string generate_sdl(const Schema& btoon_schema);
    
private:
    ConversionOptions options_;
    
    // Type mapping
    std::string map_btoon_type_to_graphql(const std::string& btoon_type);
    GraphQLType create_object_type(const std::string& name, 
                                   const std::vector<SchemaField>& fields);
    GraphQLType create_input_type(const std::string& name,
                                 const std::vector<SchemaField>& fields);
    
    // Query/Mutation generation
    void generate_queries(GraphQLSchema& schema, const Schema& btoon_schema);
    void generate_mutations(GraphQLSchema& schema, const Schema& btoon_schema);
};

/**
 * @brief Converts GraphQL schema to BTOON schema
 */
class GraphQLToBtoon {
public:
    struct ConversionOptions {
        bool strict_mode = false;
        bool preserve_directives = true;
        bool generate_validators = true;
    };
    
    GraphQLToBtoon(const ConversionOptions& options = ConversionOptions{});
    
    /**
     * @brief Convert GraphQL schema to BTOON schema
     */
    Schema convert(const GraphQLSchema& graphql_schema);
    
    /**
     * @brief Parse GraphQL SDL to BTOON schema
     */
    Schema parse_sdl(const std::string& sdl);
    
private:
    ConversionOptions options_;
    
    // Type mapping
    std::string map_graphql_type_to_btoon(const std::string& graphql_type);
    SchemaField convert_field(const GraphQLField& gql_field);
    
    // SDL parser
    GraphQLSchema parse_sdl_internal(const std::string& sdl);
};

/**
 * @brief GraphQL query executor for BTOON data
 */
class GraphQLExecutor {
public:
    GraphQLExecutor(const GraphQLSchema& schema);
    
    /**
     * @brief Execute GraphQL query on BTOON data
     */
    Value execute(const std::string& query, 
                 const Value& data,
                 const std::unordered_map<std::string, Value>& variables = {});
    
    /**
     * @brief Validate query against schema
     */
    bool validate_query(const std::string& query, 
                       std::vector<std::string>& errors) const;
    
private:
    GraphQLSchema schema_;
    
    // Query parsing and execution
    struct ParsedQuery {
        std::string operation_type;  // query, mutation, subscription
        std::string operation_name;
        std::vector<std::string> selections;
        std::unordered_map<std::string, Value> variables;
    };
    
    ParsedQuery parse_query(const std::string& query);
    Value execute_selection(const ParsedQuery& query, const Value& data);
};

/**
 * @brief GraphQL subscription handler
 */
class GraphQLSubscription {
public:
    using Callback = std::function<void(const Value&)>;
    
    GraphQLSubscription(const GraphQLSchema& schema);
    
    /**
     * @brief Subscribe to GraphQL subscription
     */
    std::string subscribe(const std::string& subscription,
                         const Callback& callback,
                         const std::unordered_map<std::string, Value>& variables = {});
    
    /**
     * @brief Unsubscribe from subscription
     */
    void unsubscribe(const std::string& subscription_id);
    
    /**
     * @brief Publish data to subscribers
     */
    void publish(const std::string& topic, const Value& data);
    
private:
    GraphQLSchema schema_;
    std::unordered_map<std::string, std::vector<std::pair<std::string, Callback>>> subscriptions_;
    std::unordered_map<std::string, ParsedQuery> subscription_queries_;
};

/**
 * @brief GraphQL federation support
 */
class GraphQLFederation {
public:
    /**
     * @brief Add federation directives to schema
     */
    static void add_federation_support(GraphQLSchema& schema);
    
    /**
     * @brief Generate federation SDL
     */
    static std::string generate_federation_sdl(const GraphQLSchema& schema);
    
    /**
     * @brief Resolve federation reference
     */
    static Value resolve_reference(const Value& reference,
                                  const std::string& type_name,
                                  const Value& data);
};

// ============= Utility Functions =============

/**
 * @brief Generate TypeScript types from GraphQL schema
 */
std::string generate_typescript_types(const GraphQLSchema& schema);

/**
 * @brief Generate Relay-compliant schema with connections
 */
GraphQLSchema make_relay_compliant(const GraphQLSchema& schema);

/**
 * @brief Add DataLoader patterns for efficient batching
 */
void add_dataloader_support(GraphQLSchema& schema);

} // namespace graphql
} // namespace btoon

#endif // BTOON_GRAPHQL_SCHEMA_H
