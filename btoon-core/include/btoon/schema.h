//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  Version 0.0.1
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
/**
 * @file schema.h
 * @brief Header file for the BTOON Schema class with versioning support.
 */
#ifndef BTOON_SCHEMA_H
#define BTOON_SCHEMA_H

#include "btoon.h"
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <unordered_map>

namespace btoon {

// Forward declarations
class SchemaImpl;
class SchemaRegistry;

/**
 * @brief Schema version representation
 */
struct SchemaVersion {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    
    SchemaVersion(uint32_t maj = 1, uint32_t min = 0, uint32_t pat = 0)
        : major(maj), minor(min), patch(pat) {}
    
    std::string toString() const;
    static SchemaVersion fromString(const std::string& str);
    
    bool operator==(const SchemaVersion& other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }
    
    bool operator<(const SchemaVersion& other) const {
        if (major != other.major) return major < other.major;
        if (minor != other.minor) return minor < other.minor;
        return patch < other.patch;
    }
    
    bool operator>(const SchemaVersion& other) const {
        return other < *this;
    }
    
    bool operator<=(const SchemaVersion& other) const {
        return !(*this > other);
    }
    
    bool operator>=(const SchemaVersion& other) const {
        return !(*this < other);
    }
};

/**
 * @brief Schema field definition
 */
struct SchemaField {
    std::string name;
    std::string type;  // "string", "int", "uint", "float", "bool", "binary", "array", "map"
    bool required = true;
    std::optional<Value> default_value;
    std::optional<std::string> description;
    std::optional<Value> constraints;  // min, max, pattern, enum, etc.
};

/**
 * @brief Schema migration function type
 */
using MigrationFunction = std::function<Value(const Value&)>;

/**
 * @brief Schema evolution strategy
 */
enum class EvolutionStrategy {
    STRICT,           // No changes allowed
    ADDITIVE,         // Only new optional fields allowed
    BACKWARD_COMPATIBLE,  // Changes that maintain backward compatibility
    FLEXIBLE          // Any changes allowed with migrations
};

/**
 * @brief Main Schema class with versioning support
 */
class Schema {
public:
    // Constructors
    Schema();
    Schema(const Value& schema_definition);
    Schema(const std::string& schema_name, const SchemaVersion& version, 
           const std::vector<SchemaField>& fields);
    ~Schema();
    
    // Move semantics
    Schema(Schema&&) noexcept;
    Schema& operator=(Schema&&) noexcept;
    
    // Delete copy semantics (use shared_ptr if needed)
    Schema(const Schema&) = delete;
    Schema& operator=(const Schema&) = delete;
    
    // Validation
    bool validate(const Value& value) const;
    std::vector<std::string> validateWithErrors(const Value& value) const;
    
    // Version management
    SchemaVersion getVersion() const;
    std::string getName() const;
    void setVersion(const SchemaVersion& version);
    
    // Field management
    void addField(const SchemaField& field);
    void removeField(const std::string& field_name);
    std::optional<SchemaField> getField(const std::string& field_name) const;
    std::vector<SchemaField> getFields() const;
    
    // Evolution and compatibility
    bool isCompatibleWith(const Schema& other) const;
    bool canMigrateTo(const Schema& target) const;
    void setEvolutionStrategy(EvolutionStrategy strategy);
    EvolutionStrategy getEvolutionStrategy() const;
    
    // Migration
    void addMigration(const SchemaVersion& from_version, 
                      const SchemaVersion& to_version,
                      MigrationFunction migration);
    std::optional<Value> migrate(const Value& value, const SchemaVersion& target_version) const;
    
    // Serialization
    Value toValue() const;
    static Schema fromValue(const Value& value);
    
    // Schema comparison
    std::vector<std::string> diff(const Schema& other) const;
    
    // Metadata
    void setDescription(const std::string& description);
    std::string getDescription() const;
    void setMetadata(const std::string& key, const Value& value);
    std::optional<Value> getMetadata(const std::string& key) const;

private:
    std::unique_ptr<SchemaImpl> pimpl_;
};

/**
 * @brief Schema Registry for managing multiple schema versions
 */
class SchemaRegistry {
public:
    SchemaRegistry();
    ~SchemaRegistry();
    
    // Move semantics
    SchemaRegistry(SchemaRegistry&&) noexcept;
    SchemaRegistry& operator=(SchemaRegistry&&) noexcept;
    
    // Delete copy semantics
    SchemaRegistry(const SchemaRegistry&) = delete;
    SchemaRegistry& operator=(const SchemaRegistry&) = delete;
    
    // Registration
    void registerSchema(const std::string& name, std::shared_ptr<Schema> schema);
    void registerSchema(std::shared_ptr<Schema> schema);  // Uses schema's name
    
    // Retrieval
    std::shared_ptr<Schema> getSchema(const std::string& name) const;
    std::shared_ptr<Schema> getSchema(const std::string& name, const SchemaVersion& version) const;
    std::vector<SchemaVersion> getVersions(const std::string& name) const;
    std::shared_ptr<Schema> getLatestSchema(const std::string& name) const;
    
    // Validation with auto-detection
    bool validate(const Value& value) const;
    std::pair<bool, std::string> validateWithSchema(const Value& value) const;
    
    // Migration
    std::optional<Value> migrateToLatest(const std::string& schema_name, 
                                         const Value& value,
                                         const SchemaVersion& from_version) const;
    
    // Management
    void removeSchema(const std::string& name);
    void removeSchema(const std::string& name, const SchemaVersion& version);
    void clear();
    
    // Persistence
    void saveToFile(const std::string& filename) const;
    void loadFromFile(const std::string& filename);
    Value toValue() const;
    static SchemaRegistry fromValue(const Value& value);

private:
    class RegistryImpl;
    std::unique_ptr<RegistryImpl> pimpl_;
};

/**
 * @brief Schema builder for fluent API
 */
class SchemaBuilder {
public:
    SchemaBuilder(const std::string& name);
    
    SchemaBuilder& version(uint32_t major, uint32_t minor = 0, uint32_t patch = 0);
    SchemaBuilder& version(const SchemaVersion& v);
    SchemaBuilder& description(const std::string& desc);
    
    SchemaBuilder& field(const std::string& name, const std::string& type);
    SchemaBuilder& field(const SchemaField& field);
    SchemaBuilder& optionalField(const std::string& name, const std::string& type, 
                                 const Value& default_value = Nil{});
    
    SchemaBuilder& withConstraint(const std::string& field_name, const Value& constraint);
    SchemaBuilder& withDescription(const std::string& field_name, const std::string& desc);
    
    SchemaBuilder& evolutionStrategy(EvolutionStrategy strategy);
    SchemaBuilder& metadata(const std::string& key, const Value& value);
    
    std::shared_ptr<Schema> build();

private:
    std::string name_;
    SchemaVersion version_;
    std::string description_;
    std::vector<SchemaField> fields_;
    EvolutionStrategy strategy_ = EvolutionStrategy::BACKWARD_COMPATIBLE;
    std::unordered_map<std::string, Value> metadata_;
};

/**
 * @brief Predefined schema types for common use cases
 */
namespace schemas {
    std::shared_ptr<Schema> createTimeSeries();
    std::shared_ptr<Schema> createKeyValue();
    std::shared_ptr<Schema> createDocument();
    std::shared_ptr<Schema> createTable(const std::vector<SchemaField>& columns);
}

/**
 * @brief Schema inference options
 */
struct InferenceOptions {
    bool infer_constraints = true;      // Infer min/max, pattern, enum
    bool strict_types = false;          // If false, allow type variations (e.g., int/uint)
    size_t sample_size = 1000;          // Max number of samples to analyze
    double required_threshold = 0.95;   // Field presence threshold for required
    size_t max_enum_values = 10;        // Max unique values to consider as enum
    bool infer_patterns = true;         // Try to detect string patterns
    bool merge_numeric_types = true;    // Treat int/uint/float as compatible
};

/**
 * @brief Schema inference engine
 */
class SchemaInferrer {
public:
    SchemaInferrer(const InferenceOptions& options = InferenceOptions{});
    ~SchemaInferrer();
    
    // Move semantics
    SchemaInferrer(SchemaInferrer&&) noexcept;
    SchemaInferrer& operator=(SchemaInferrer&&) noexcept;
    
    // Delete copy semantics
    SchemaInferrer(const SchemaInferrer&) = delete;
    SchemaInferrer& operator=(const SchemaInferrer&) = delete;
    
    /**
     * @brief Infer schema from a single value
     */
    Schema infer(const Value& value, const std::string& name = "InferredSchema");
    
    /**
     * @brief Infer schema from array of similar objects
     */
    Schema inferFromArray(const Array& array, const std::string& name = "InferredSchema");
    
    /**
     * @brief Merge multiple inferred schemas
     */
    Schema merge(std::vector<Schema>&& schemas, const std::string& name = "MergedSchema");
    
    /**
     * @brief Get inference statistics
     */
    struct Statistics {
        size_t samples_analyzed;
        size_t fields_discovered;
        size_t optional_fields;
        size_t enum_fields;
        std::unordered_map<std::string, std::string> field_types;
        std::unordered_map<std::string, double> field_presence_ratio;
    };
    
    Statistics getStatistics() const;
    
private:
    std::unique_ptr<class SchemaInferrerImpl> pimpl_;
};

/**
 * @brief Convenience function to infer schema from data
 */
Schema inferSchema(const Value& value, const InferenceOptions& options = InferenceOptions{});

} // namespace btoon

#endif // BTOON_SCHEMA_H