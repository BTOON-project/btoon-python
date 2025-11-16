/**
 * @file graph.h
 * @brief Graph data structures and algorithms for BTOON
 * 
 * Provides efficient representation and manipulation of graph data,
 * including directed/undirected graphs, weighted edges, and graph algorithms.
 */

#ifndef BTOON_GRAPH_H
#define BTOON_GRAPH_H

#include "btoon/btoon.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <limits>
#include <optional>
#include <algorithm>

namespace btoon {
namespace graph {

/**
 * @brief Graph node with properties
 */
template<typename NodeId = std::string, typename NodeData = Value>
struct Node {
    NodeId id;
    NodeData data;
    std::unordered_map<std::string, Value> properties;
    
    Node() = default;
    Node(const NodeId& id) : id(id) {}
    Node(const NodeId& id, const NodeData& data) : id(id), data(data) {}
};

/**
 * @brief Graph edge with weight and properties
 */
template<typename NodeId = std::string, typename Weight = double>
struct Edge {
    NodeId from;
    NodeId to;
    Weight weight;
    std::unordered_map<std::string, Value> properties;
    
    Edge() : weight(Weight{1}) {}
    Edge(const NodeId& f, const NodeId& t, Weight w = Weight{1})
        : from(f), to(t), weight(w) {}
};

/**
 * @brief Graph type enumeration
 */
enum class GraphType {
    DIRECTED,
    UNDIRECTED,
    DIRECTED_ACYCLIC,
    TREE,
    BIPARTITE,
    WEIGHTED,
    MULTIGRAPH
};

/**
 * @brief Graph representation format
 */
enum class GraphFormat {
    ADJACENCY_LIST,
    ADJACENCY_MATRIX,
    EDGE_LIST,
    INCIDENCE_MATRIX,
    COMPRESSED_SPARSE
};

/**
 * @brief Generic graph data structure
 */
template<typename NodeId = std::string, typename NodeData = Value, typename Weight = double>
class Graph {
public:
    using NodeType = Node<NodeId, NodeData>;
    using EdgeType = Edge<NodeId, Weight>;
    using AdjacencyList = std::unordered_map<NodeId, std::vector<std::pair<NodeId, Weight>>>;
    
    Graph(GraphType type = GraphType::DIRECTED) : type_(type) {}
    
    // Node operations
    void add_node(const NodeId& id, const NodeData& data = NodeData{});
    void remove_node(const NodeId& id);
    bool has_node(const NodeId& id) const;
    NodeType* get_node(const NodeId& id);
    const NodeType* get_node(const NodeId& id) const;
    size_t node_count() const { return nodes_.size(); }
    
    // Edge operations
    void add_edge(const NodeId& from, const NodeId& to, Weight weight = Weight{1});
    void remove_edge(const NodeId& from, const NodeId& to);
    bool has_edge(const NodeId& from, const NodeId& to) const;
    Weight get_edge_weight(const NodeId& from, const NodeId& to) const;
    size_t edge_count() const;
    
    // Graph properties
    size_t degree(const NodeId& node) const;
    size_t in_degree(const NodeId& node) const;
    size_t out_degree(const NodeId& node) const;
    std::vector<NodeId> neighbors(const NodeId& node) const;
    std::vector<NodeId> predecessors(const NodeId& node) const;
    
    // Graph algorithms
    std::vector<NodeId> topological_sort() const;
    std::unordered_map<NodeId, Weight> dijkstra(const NodeId& source) const;
    std::unordered_map<NodeId, NodeId> bfs(const NodeId& source) const;
    std::unordered_map<NodeId, NodeId> dfs(const NodeId& source) const;
    bool has_cycle() const;
    std::vector<std::vector<NodeId>> connected_components() const;
    std::vector<std::vector<NodeId>> strongly_connected_components() const;
    
    // Minimum spanning tree
    std::vector<EdgeType> kruskal_mst() const;
    std::vector<EdgeType> prim_mst(const NodeId& start) const;
    
    // Shortest paths
    std::vector<NodeId> shortest_path(const NodeId& from, const NodeId& to) const;
    std::unordered_map<NodeId, std::unordered_map<NodeId, Weight>> floyd_warshall() const;
    
    // Graph metrics
    double clustering_coefficient(const NodeId& node) const;
    double average_clustering_coefficient() const;
    size_t diameter() const;
    double density() const;
    
    // Serialization
    Value to_btoon() const;
    static Graph from_btoon(const Value& value);
    
    // Export formats
    std::string to_dot() const;  // GraphViz format
    std::string to_gml() const;  // Graph Modeling Language
    std::string to_graphml() const;  // GraphML XML format
    Value to_cytoscape_json() const;  // Cytoscape.js format
    
private:
    GraphType type_;
    std::unordered_map<NodeId, NodeType> nodes_;
    AdjacencyList adjacency_;
    
    // Helper methods
    void dfs_visit(const NodeId& node, 
                   std::unordered_set<NodeId>& visited,
                   std::vector<NodeId>& result) const;
};

/**
 * @brief Specialized tree structure
 */
template<typename NodeId = std::string, typename NodeData = Value>
class Tree : public Graph<NodeId, NodeData, double> {
public:
    Tree() : Graph<NodeId, NodeData, double>(GraphType::TREE) {}
    
    void set_root(const NodeId& root) { root_ = root; }
    const NodeId& root() const { return root_; }
    
    NodeId parent(const NodeId& node) const;
    std::vector<NodeId> children(const NodeId& node) const;
    std::vector<NodeId> ancestors(const NodeId& node) const;
    std::vector<NodeId> descendants(const NodeId& node) const;
    
    size_t depth(const NodeId& node) const;
    size_t height() const;
    bool is_leaf(const NodeId& node) const;
    
    // Tree traversals
    std::vector<NodeId> preorder() const;
    std::vector<NodeId> inorder() const;
    std::vector<NodeId> postorder() const;
    std::vector<NodeId> levelorder() const;
    
private:
    NodeId root_;
};

/**
 * @brief Property graph for complex relationships
 */
class PropertyGraph {
public:
    struct PropertyNode {
        std::string id;
        std::string label;
        std::unordered_map<std::string, Value> properties;
    };
    
    struct PropertyEdge {
        std::string id;
        std::string from;
        std::string to;
        std::string type;
        std::unordered_map<std::string, Value> properties;
    };
    
    // CRUD operations
    void add_node(const PropertyNode& node);
    void add_edge(const PropertyEdge& edge);
    void update_node_property(const std::string& node_id, 
                              const std::string& key, 
                              const Value& value);
    void update_edge_property(const std::string& edge_id,
                             const std::string& key,
                             const Value& value);
    
    // Query operations
    std::vector<PropertyNode> query_nodes(
        const std::unordered_map<std::string, Value>& criteria) const;
    std::vector<PropertyEdge> query_edges(
        const std::unordered_map<std::string, Value>& criteria) const;
    
    // Pattern matching (Cypher-like)
    struct Pattern {
        std::optional<std::string> node_label;
        std::optional<std::string> edge_type;
        std::unordered_map<std::string, Value> properties;
    };
    
    std::vector<std::unordered_map<std::string, Value>> match_pattern(
        const std::vector<Pattern>& patterns) const;
    
    // Graph algorithms on property graphs
    std::vector<PropertyNode> shortest_path_with_properties(
        const std::string& from,
        const std::string& to,
        const std::function<double(const PropertyEdge&)>& weight_func) const;
    
    // Serialization
    Value to_btoon() const;
    static PropertyGraph from_btoon(const Value& value);
    
private:
    std::unordered_map<std::string, PropertyNode> nodes_;
    std::unordered_map<std::string, PropertyEdge> edges_;
    std::unordered_map<std::string, std::vector<std::string>> node_edges_;
};

/**
 * @brief Graph layout algorithms for visualization
 */
class GraphLayout {
public:
    struct Position {
        double x, y, z;
        Position(double x = 0, double y = 0, double z = 0) : x(x), y(y), z(z) {}
    };
    
    template<typename NodeId>
    using Layout = std::unordered_map<NodeId, Position>;
    
    // Layout algorithms
    template<typename NodeId, typename NodeData, typename Weight>
    static Layout<NodeId> force_directed(
        const Graph<NodeId, NodeData, Weight>& graph,
        size_t iterations = 100);
    
    template<typename NodeId, typename NodeData, typename Weight>
    static Layout<NodeId> circular(
        const Graph<NodeId, NodeData, Weight>& graph);
    
    template<typename NodeId, typename NodeData, typename Weight>
    static Layout<NodeId> hierarchical(
        const Graph<NodeId, NodeData, Weight>& graph);
    
    template<typename NodeId, typename NodeData, typename Weight>
    static Layout<NodeId> spectral(
        const Graph<NodeId, NodeData, Weight>& graph);
};

/**
 * @brief Graph builders for common graph types
 */
class GraphBuilder {
public:
    // Create common graph structures
    static Graph<size_t> complete_graph(size_t n);
    static Graph<size_t> cycle_graph(size_t n);
    static Graph<size_t> path_graph(size_t n);
    static Graph<size_t> star_graph(size_t n);
    static Graph<size_t> wheel_graph(size_t n);
    static Graph<size_t> grid_graph(size_t rows, size_t cols);
    static Graph<size_t> hypercube_graph(size_t dimensions);
    
    // Random graphs
    static Graph<size_t> random_graph(size_t nodes, double edge_probability);
    static Graph<size_t> barabasi_albert_graph(size_t nodes, size_t edges_per_node);
    static Graph<size_t> watts_strogatz_graph(size_t nodes, size_t k, double p);
};

/**
 * @brief Graph metrics and analysis
 */
class GraphMetrics {
public:
    template<typename NodeId, typename NodeData, typename Weight>
    static std::unordered_map<NodeId, double> betweenness_centrality(
        const Graph<NodeId, NodeData, Weight>& graph);
    
    template<typename NodeId, typename NodeData, typename Weight>
    static std::unordered_map<NodeId, double> closeness_centrality(
        const Graph<NodeId, NodeData, Weight>& graph);
    
    template<typename NodeId, typename NodeData, typename Weight>
    static std::unordered_map<NodeId, double> eigenvector_centrality(
        const Graph<NodeId, NodeData, Weight>& graph,
        size_t iterations = 100);
    
    template<typename NodeId, typename NodeData, typename Weight>
    static std::unordered_map<NodeId, double> pagerank(
        const Graph<NodeId, NodeData, Weight>& graph,
        double damping = 0.85,
        size_t iterations = 100);
    
    template<typename NodeId, typename NodeData, typename Weight>
    static std::vector<std::vector<NodeId>> communities(
        const Graph<NodeId, NodeData, Weight>& graph);
};

// ============= Utility Functions =============

/**
 * @brief Convert graph to tabular format for analysis
 */
template<typename NodeId, typename NodeData, typename Weight>
Value graph_to_table(const Graph<NodeId, NodeData, Weight>& graph);

/**
 * @brief Build graph from edge list
 */
template<typename NodeId, typename Weight>
Graph<NodeId, Value, Weight> from_edge_list(
    const std::vector<std::tuple<NodeId, NodeId, Weight>>& edges);

/**
 * @brief Graph isomorphism check
 */
template<typename NodeId, typename NodeData, typename Weight>
bool are_isomorphic(const Graph<NodeId, NodeData, Weight>& g1,
                   const Graph<NodeId, NodeData, Weight>& g2);

} // namespace graph
} // namespace btoon

#endif // BTOON_GRAPH_H
