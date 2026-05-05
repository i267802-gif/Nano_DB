#include "Graph.h"

namespace nanodb {

// Disjoint-set / Union-Find with path compression + union-by-rank.
class DSU {
public:
    DSU(int n) {
        parent_ = new int[n];
        rank_ = new int[n];
        for (int i = 0; i < n; ++i) { parent_[i] = i; rank_[i] = 0; }
    }
    ~DSU() { delete[] parent_; delete[] rank_; }

    int find(int x) {
        while (parent_[x] != x) {
            parent_[x] = parent_[parent_[x]]; // path compression
            x = parent_[x];
        }
        return x;
    }

    bool unite(int a, int b) {
        int ra = find(a), rb = find(b);
        if (ra == rb) return false;
        if (rank_[ra] < rank_[rb]) { int t = ra; ra = rb; rb = t; }
        parent_[rb] = ra;
        if (rank_[ra] == rank_[rb]) ++rank_[ra];
        return true;
    }
private:
    int* parent_;
    int* rank_;
};

DynArray<Edge> Graph::kruskalMST() const {
    DynArray<Edge> sorted = edges_;
    sorted.sort([](const Edge& a, const Edge& b) { return a.weight < b.weight; });

    DSU dsu((int)nodes_.size());
    DynArray<Edge> mst;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const Edge& e = sorted[i];
        if (dsu.unite(e.u, e.v)) {
            mst.push_back(e);
            if (mst.size() + 1 == nodes_.size()) break;
        }
    }
    return mst;
}

} // namespace nanodb
