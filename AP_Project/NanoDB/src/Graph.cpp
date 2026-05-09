#include "Graph.h"

namespace nanodb {

// Disjoint-Set Union with union-by-rank and two-pass path compression.
// Two-pass (Tarjan) compression points every node on the find-path directly
// to the root in one call, giving amortised O(alpha(n)) per operation versus
// O(log n) for the single-pass path-halving used previously.
class DSU {
public:
    explicit DSU(int n) : n_(n) {
        parent_ = new int[n];
        rank_   = new int[n];
        for (int i = 0; i < n; ++i) { parent_[i] = i; rank_[i] = 0; }
    }
    ~DSU() { delete[] parent_; delete[] rank_; }

    // Two-pass path compression: first walk to the root, then flatten.
    int find(int x) {
        // Pass 1: locate root.
        int root = x;
        while (parent_[root] != root) root = parent_[root];
        // Pass 2: point every node on the path directly to root.
        while (parent_[x] != root) {
            int next   = parent_[x];
            parent_[x] = root;
            x          = next;
        }
        return root;
    }

    // Union by rank: attach the smaller tree under the larger one.
    bool unite(int a, int b) {
        int ra = find(a), rb = find(b);
        if (ra == rb) return false;
        if (rank_[ra] < rank_[rb]) { int t = ra; ra = rb; rb = t; }
        parent_[rb] = ra;
        if (rank_[ra] == rank_[rb]) ++rank_[ra];
        return true;
    }

    int size() const { return n_; }

private:
    int* parent_;
    int* rank_;
    int  n_;
};

DynArray<Edge> Graph::kruskalMST() const {
    // Trivial cases: a graph with 0 or 1 node has no edges in its MST.
    if (nodes_.size() <= 1) return DynArray<Edge>();

    // Sort a copy of the edge list by ascending weight.
    DynArray<Edge> sorted = edges_;
    sorted.sort([](const Edge& a, const Edge& b) { return a.weight < b.weight; });

    DSU dsu((int)nodes_.size());

    // Pre-allocate exactly (nodeCount - 1) slots — a spanning tree on N nodes
    // has exactly N-1 edges.
    DynArray<Edge> mst;
    mst.reserve(nodes_.size() - 1);

    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const Edge& e = sorted[i];
        // Guard against out-of-range node indices.
        if (e.u < 0 || e.v < 0 ||
            e.u >= dsu.size() || e.v >= dsu.size()) continue;

        if (dsu.unite(e.u, e.v)) {
            mst.push_back(e);
            // A spanning tree on N nodes needs exactly N-1 edges; stop early.
            if (mst.size() + 1 == nodes_.size()) break;
        }
    }
    return mst;
}

} // namespace nanodb
