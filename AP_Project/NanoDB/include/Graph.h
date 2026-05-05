#pragma once
// Tiny weighted undirected graph with Kruskal's MST.
// We use it to model joins: each node = a table, each edge weight = an
// estimate of the join cost between two tables (smaller -> cheaper).
// The MST gives the cheapest spanning order of the join.

#include "DynArray.h"
#include "String.h"

namespace nanodb {

struct Edge {
    int u;
    int v;
    double weight;
    String label;
    Edge() : u(0), v(0), weight(0.0), label("") {}
    Edge(int a, int b, double w, const String& l) : u(a), v(b), weight(w), label(l) {}
};

class Graph {
public:
    Graph() {}

    int addNode(const String& name) {
        nodes_.push_back(name);
        return (int)nodes_.size() - 1;
    }

    void addEdge(int u, int v, double w, const String& label) {
        edges_.push_back(Edge(u, v, w, label));
    }

    std::size_t nodeCount() const { return nodes_.size(); }
    std::size_t edgeCount() const { return edges_.size(); }

    const String& nodeName(int i) const { return nodes_[(std::size_t)i]; }
    const Edge&   edge(std::size_t i) const { return edges_[i]; }

    // Kruskal's MST. Returns selected edges (size = nodeCount-1 if connected).
    DynArray<Edge> kruskalMST() const;

private:
    DynArray<String> nodes_;
    DynArray<Edge>   edges_;
};

} // namespace nanodb
