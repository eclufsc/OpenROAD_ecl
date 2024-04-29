#pragma once

#include <vector>
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/graph/grid_graph.hpp>
#include <unordered_map>

#include "odb/db.h"

namespace odb {
  class dbDatabase;
  class dbInst;
  class dbRow;
}

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;
typedef bg::model::point<int64_t, 2, bg::cs::cartesian> point_t;

namespace rcm {

class Abacus {
    using Cell = std::pair<odb::Rect, odb::dbInst*>;
    using Row = std::pair<odb::Rect, int>;
    using Split = std::pair<int, int>;

public:
    Abacus();
    
    std::vector<odb::dbInst *> abacus(int x1, int y1, int x2, int y2);
    std::vector<odb::dbInst *> abacus(
        std::vector<Row> const& rows,
        std::vector<std::vector<Split>> const& splits_per_row,
        std::vector<Cell>* cells
    );

    std::vector<std::pair<int, int>> get_free_spaces(int x1, int y1, int x2, int y2);

    bool failed() { return failed_; };

    void InitRowTree();



private:
    // Define a 2D cartesian point using geometry box of DBUs.
    typedef bg::model::box<point_t> box_t;
    // Define RTree type of DBU box using R-Star algorithm.
    typedef std::pair<box_t, odb::dbRow*> RowElement;
    typedef bgi::rtree<RowElement, bgi::rstar<16>> RowTree;

    struct AbacusCluster {
        double q;
        double weight;
        int width;
        int x;
        int last_cell;
    };

    struct AbacusCell {
        int id;
        odb::Rect global_pos;
        double weight;
    };

    bool abacus_try_add_cell(
        odb::Rect row, int site_width,
        AbacusCell const& cell,
        std::vector<AbacusCluster> const& clusters,
        AbacusCluster* new_cluster, int* previous_i
    );
    bool abacus_try_place_and_collapse(
        std::vector<AbacusCluster> const& clusters,
        odb::Rect row, int site_width,
        AbacusCluster* new_cluster, int* previous_i
    );

    bool set_pos(odb::dbInst* cell, int x, int y);

    bool collide(int pos1_min, int pos1_max, int pos2_min, int pos2_max);

    auto sort_and_get_splits(
        std::vector<Row>* rows,
        std::vector<odb::Rect> const& fixed_cells
    ) -> std::vector<std::vector<Split>>;

    // attributes
    bool failed_ = false;
    std::unique_ptr<RowTree> rowTree_;
    odb::dbDatabase* db;
};
}