#include "rcm/CellMoveRouter.h"
#include "grt/PinGridLocation.h"
#include "odb/db.h"
#include "gui/gui.h"
#include "odb/dbObject.h"
#include "odb/dbTypes.h"
#include "odb/geom.h"
#include "ord/OpenRoad.hh"
#include "sta/Delay.hh"
#include "est/EstimateParasitics.h"
#include "sta/MinMax.hh"
#include "utl/Logger.h"
#include "grt/GlobalRouter.h"
#include "stt/SteinerTreeBuilder.h"
#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "sta/Liberty.hh"
#include "sta/Graph.hh"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <ostream>
#include <random>
#include <unordered_map>
#include <vector>
#include <fstream>


namespace rcm {

class RectangleRender : public gui::Renderer
{
public:
  virtual void drawObjects(gui::Painter& /* painter */) override;

  void addRectangle(odb::Rect rect){ rectangles_.push_back(rect); };
  void clear_rectangles();
  
private:
  std::vector<odb::Rect> rectangles_;
};

void
RectangleRender::drawObjects(gui::Painter &painter)
{
  for(int i = 0; i < rectangles_.size(); i++)
  {
    auto color = gui::Painter::Color(255, 0, 0, 40);
    if (i == rectangles_.size()-1)
    {
      painter.setBrush(color);// Try other colors
      painter.drawRect(rectangles_[i]);
    } else
    {
      painter.setBrush(color);// Try other colors
      painter.drawRect(rectangles_[i]);
    }
  }
}

void
RectangleRender::clear_rectangles()
{
  rectangles_.clear();
}


CellMoveRouter::CellMoveRouter():
  db_{ord::OpenRoad::openRoad()->getDb()},
  logger_{ord::OpenRoad::openRoad()->getLogger()},
  grt_{ord::OpenRoad::openRoad()->getGlobalRouter()},
  debug_{false},
  openSta_{ord::OpenRoad::openRoad()->getSta()},
  network_{ord::OpenRoad::openRoad()->getDbNetwork()},
  estimate_parasitics_{ord::OpenRoad::openRoad()->getEstimateParasitics()}
{
}

void
CellMoveRouter::drawRectangle(int x1, int y1, int x2, int y2)
{
  gui::Gui* gui = gui::Gui::get();
  if (rectangleRender_ == nullptr)
  {
    rectangleRender_ = std::make_unique<RectangleRender>();
    gui->registerRenderer(rectangleRender_.get());
  }
  odb::Rect rect{x1, y1, x2, y2};
  rectangleRender_->addRectangle(rect);
  gui->redraw();
}

void CellMoveRouter::reportCellSlack()
{
  std::ofstream outFile("output.txt");
  if (!outFile) {
      std::cerr << "Error opening file!" << std::endl;
  }

  auto block = db_->getChip()->getBlock();
  openSta_->ensureGraph();
  openSta_->searchPreamble();
  for (auto mode : openSta_->modes()) {
    openSta_->ensureClkNetwork(mode);
  }
  auto timin_graph = openSta_->graph();
  for(auto cell : block->getInsts()) {
    outFile << "Cell " << cell->getName() << ":" <<std::endl;
    for(auto iterm : cell->getITerms()) {
      if (!iterm->getNet() || (iterm->getNet()->getSigType() == odb::dbSigType::GROUND)
      || (iterm->getNet()->getSigType() == odb::dbSigType::POWER)) {
        continue;
      }        
      outFile << "  Pin " << iterm->getName() << ":" <<std::endl;
      auto pin = network_->dbToSta(iterm);
      sta::Vertex * vertex = timin_graph->pinLoadVertex(pin);
      auto setup_slack = openSta_->slack(vertex, sta::MinMax::max());
      auto hold_slack = openSta_->slack(vertex, sta::MinMax::min());
      outFile << "    setup slack = " << setup_slack << std::endl;
      outFile << "    hold slack = " << hold_slack << std::endl;
    }
    outFile << "--------------- = " << std::endl;
  }

  outFile.close();  // Always close when done
}

bool CellMoveRouter::isHorizontal(const grt::GSegment& seg)
{
  return seg.init_y == seg.final_y && seg.init_layer == seg.final_layer;
}

bool CellMoveRouter::isVertical(const grt::GSegment& seg)
{
  return seg.init_x == seg.final_x && seg.init_layer == seg.final_layer;
}

bool CellMoveRouter::computeIntersection(const grt::GSegment& h_seg,
                                const grt::GSegment& v_seg,
                                Node&           intersection)
{
  if (h_seg.init_layer != v_seg.init_layer) {
    return false;
  }
  int hx_min = std::min(h_seg.init_x, h_seg.final_x);
  int hx_max = std::max(h_seg.init_x, h_seg.final_x);
  int vy_min = std::min(v_seg.init_y, v_seg.final_y);
  int vy_max = std::max(v_seg.init_y, v_seg.final_y);

  int vx = v_seg.init_x; // vertical segment has constant x
  int hy = h_seg.init_y; // horizontal segment has constant y

  if (vx >= hx_min && vx <= hx_max &&
      hy >= vy_min && hy <= vy_max)
  {
    intersection = { vx, hy, h_seg.init_layer };
    return true;
  }
  return false;
}

bool CellMoveRouter::isPointOnSegment(const Node& point, const grt::GSegment& seg)
{
  auto [px, py, pl] = point;

  if (seg.isVia()) {
    return false; // vias only connect their two endpoints
  }
  if (isHorizontal(seg)) {
    if (pl != seg.init_layer || py != seg.init_y) {
      return false;
    }
    int mn = std::min(seg.init_x, seg.final_x);
    int mx = std::max(seg.init_x, seg.final_x);
    return px >= mn && px <= mx;
  }

  if (isVertical(seg)) {
    if (pl != seg.init_layer || px != seg.init_x) {
      return false;
    }
    int mn = std::min(seg.init_y, seg.final_y);
    int mx = std::max(seg.init_y, seg.final_y);
    return py >= mn && py <= mx;
  }

  return false;
}

void CellMoveRouter::addEdge(RoutingGraph&   graph,
                    const Node&     from,
                    const Node&     to,
                    const grt::GSegment& original_seg)
{
  if (from == to) {
    return;
  }

  auto [fx, fy, fl] = from;
  auto [tx, ty, tl] = to;

  GraphEdge edge;
  edge.from        = from;
  edge.to          = to;
  edge.length      = std::abs(fx - tx) + std::abs(fy - ty);
  edge.is_via      = original_seg.isVia();
  edge.is_jumper   = original_seg.isJumper();
  edge.is_3d_route = original_seg.is3DRoute();

  int idx = static_cast<int>(graph.edges.size());
  graph.edges.push_back(edge);
  graph.adjacency[from].push_back(idx);
  graph.adjacency[to].push_back(idx);
}

void CellMoveRouter::registerNode(RoutingGraph& graph, const Node& node)
{
    if (!graph.nodes.count(node)) {
        auto [x, y, layer] = node;
        graph.nodes[node]  = NodeData{ x, y, layer };
    }
}

// ─────────────────────────────────────────────
//  Main function
// ─────────────────────────────────────────────

RoutingGraph CellMoveRouter::buildGraphFromGRoute(odb::dbNet* net)
{
  grt::GRoute route = grt_->getNetRoute(net);
  std::vector<grt::PinGridLocation> net_pins_position = grt_->getPinGridPositions(net);
  RoutingGraph graph;

  // ── Step 1: Register all segment endpoints ──────────────────────────
  for (const grt::GSegment& seg : route) {
    registerNode(graph, { seg.init_x,  seg.init_y,  seg.init_layer  });
    registerNode(graph, { seg.final_x, seg.final_y, seg.final_layer });
  }
  // ── Step 1.5: Register all pin positions ──────────────────────────
  for (grt::PinGridLocation pin : net_pins_position) {
    std::string pin_name = pin.bterm == nullptr ? pin.iterm->getName() : pin.bterm->getName();
    registerNode(graph, { pin.grid_pt.getX(), pin.grid_pt.getY(), pin.conn_layer});

    Node pin_pos = { pin.grid_pt.getX(), pin.grid_pt.getY(), pin.conn_layer};
    // Check that pin lies on at least one segment (sanity check)
    bool covered = false;
    for (const grt::GSegment& seg : route) {
        if (isPointOnSegment(pin_pos, seg)) {
            covered = true;
            break;
        }

        // Also check via endpoints directly
        if (seg.isVia()) {
            Node via_from = { seg.init_x,  seg.init_y,  seg.init_layer  };
            Node via_to   = { seg.final_x, seg.final_y, seg.final_layer };
            if (pin_pos == via_from || pin_pos == via_to) {
                covered = true;
                break;
            }
        }
    }

    if (!covered) {
        auto [x, y, layer] = pin_pos;
        std::cout << "[ERROR] Pin "<< pin_name <<" from net: " << net->getName()
                  << ", at (" << x << ", " << y
                  << ", layer=" << layer << ") is not covered "
                  << "by any segment — skipping.\n";
        continue;
        }
  }

  // ── Step 2: Compute and register cross/T-junction intersections ─────
  //    Compares every horizontal segment against every vertical segment.
  for (int i = 0; i < static_cast<int>(route.size()); i++) {
    for (int j = i + 1; j < static_cast<int>(route.size()); j++) {
      const grt::GSegment& a = route[i];
      const grt::GSegment& b = route[j];

      Node intersection;
      if (isHorizontal(a) && isVertical(b)) {
        if (computeIntersection(a, b, intersection)) {
          registerNode(graph, intersection);
        }
      } else if (isVertical(a) && isHorizontal(b)) {
        if (computeIntersection(b, a, intersection)) {
          registerNode(graph, intersection);
        }
      }
    }
  }

  // ── Step 3: Split each segment at every node that lies on it ────────
  for (const grt::GSegment& seg : route)
  {
    if (seg.isVia()) {
      // Vias just connect their two endpoints directly
      Node from = { seg.init_x, seg.init_y, seg.init_layer  };
      Node to   = { seg.final_x, seg.final_y, seg.final_layer };
      addEdge(graph, from, to, seg);
      continue;
    }

    // Collect all nodes that fall on this segment
    std::vector<Node> points;
    for (const auto& [node, data] : graph.nodes) {
      if (isPointOnSegment(node, seg)) {
        points.push_back(node);
      }
    }
    // Sort from segment start toward segment end
    bool horiz = isHorizontal(seg);
    std::sort(points.begin(), points.end(),
        [&](const Node& a, const Node& b) {
            return horiz ? std::get<0>(a) < std::get<0>(b)
                          : std::get<1>(a) < std::get<1>(b);
        });

    // Reverse if segment goes in the negative direction
    if (horiz && seg.init_x > seg.final_x){
      std::reverse(points.begin(), points.end());
    } else if (!horiz && seg.init_y > seg.final_y){
      std::reverse(points.begin(), points.end());
    }

    // Create one edge per consecutive pair of points
    for (int k = 0; k + 1 < static_cast<int>(points.size()); k++) {
      addEdge(graph, points[k], points[k + 1], seg);
    }
  }

  return graph;
}

void CellMoveRouter::writeGraphToFile(std::map<odb::dbNet*, RoutingGraph>& routing_grpahs, const std::string& filename)
{
  std::ofstream out(filename);
  if (!out) {
    std::cerr << "Error opening file: " << filename << std::endl;
    return;
  }
  for(auto [net, graph] : routing_grpahs) {
    out << "=== Graph for net: " << net->getName() << " ===\n";
    out << "=== NODES (" << graph.nodes.size() << ") ===\n";
    for (const auto& [node, data] : graph.nodes) {
      const auto& [x, y, layer] = node;
      out << "  (" << x << ", " << y << ", layer=" << layer << ")\n";
    }

    out << "\n=== EDGES (" << graph.edges.size() << ") ===\n";
    for (int i = 0; i < graph.edges.size(); i++) {
      const GraphEdge& e = graph.edges[i];
      auto [fx, fy, fl] = e.from;
      auto [tx, ty, tl] = e.to;

      out << "  [" << i << "] "
          << "(" << fx << "," << fy << ",l=" << fl << ")"
          << " -> "
          << "(" << tx << "," << ty << ",l=" << tl << ")"
          << "  len="      << e.length
          << "  via="      << e.is_via
          << "  jumper="   << e.is_jumper
          << "  3d="       << e.is_3d_route
          << "\n";
    }

    out << "\n=== ADJACENCY ===\n";
    for (const auto& [node, edge_indices] : graph.adjacency) {
      auto [x, y, layer] = node;
      out << "  (" << x << "," << y << ",l=" << layer << ") -> edges: ";
      for (int idx : edge_indices) {
        out << idx << " ";
      }
      out << "\n";
    }
  }
  out.close();
}

int CellMoveRouter::getWl2Sink(RoutingGraph& graph, const odb::dbITerm* sink)
{
  odb::dbNet* sink_net = sink->getNet();
  for (grt::PinGridLocation pin : grt_->getPinGridPositions(sink_net)) {
    if ((pin.iterm != nullptr && pin.iterm->getName() == sink->getName())) {
      NodeData data = graph.nodes[{ pin.grid_pt.getX(), pin.grid_pt.getY(), pin.conn_layer}];
      return data.distance;
    }
  }
  return -1;
}

void CellMoveRouter::computeDistancesFromSource(RoutingGraph& graph, const Node& source)
{
  if (!graph.nodes.count(source)) {
    auto [x, y, layer] = source;
    std::cerr << "[ERROR] Source node (" << x << ", " << y
              << ", layer=" << layer << ") not found in graph.\n";
    return;
  }

  /* ── Reset all distances ──────────────────────────────────────────────
  for (auto& [node, data] : graph.nodes) {
      data.distance = std::numeric_limits<int>::max();
      data.visited  = false;
  }*/

  // ── DFS using a stack ────────────────────────────────────────────────
  // Each entry is (node, accumulated_distance)
  std::stack<std::pair<Node, int64_t>> stack;

  graph.nodes[source].distance = 0;
  stack.push({ source, 0 });

  while (!stack.empty()) {
    auto [current, dist] = stack.top();
    stack.pop();

    NodeData& current_data = graph.nodes[current];
    if (current_data.visited) {
      continue;
    }
    current_data.visited = true;

    for (int edge_idx : graph.adjacency[current]) {
      const GraphEdge& edge     = graph.edges[edge_idx];
      const Node&      neighbor = (edge.from == current) ? edge.to : edge.from;

      NodeData& neighbor_data = graph.nodes[neighbor];
      if (neighbor_data.visited) continue;

      // In a tree, this is always the shortest (only) path
      neighbor_data.distance = dist + edge.length;
      stack.push({ neighbor, neighbor_data.distance });
    }
  }
}

void CellMoveRouter::populateRoutingGraphs()
{
  auto block = db_->getChip()->getBlock();
  for(odb::dbNet* net : block->getNets()) {
    if ((net->getSigType() == odb::dbSigType::GROUND) ||
        (net->getSigType() == odb::dbSigType::POWER)) {
      continue;
    }
    if(net->getITermCount() + net->getBTermCount() < 2) {
      continue;
    }
    routing_grpahs_[net] = buildGraphFromGRoute(net);
    odb::dbObject* driver = net->getFirstDriverTerm();
    for (grt::PinGridLocation pin : grt_->getPinGridPositions(net)) {
      if ((pin.bterm != nullptr && pin.bterm->getName() == driver->getName()) ||
          (pin.iterm != nullptr && pin.iterm->getName() == driver->getName())) {
        computeDistancesFromSource(routing_grpahs_[net], { pin.grid_pt.getX(), pin.grid_pt.getY(), pin.conn_layer});
        break;
      }
    }
  }
}

void CellMoveRouter::findCandidatePaths()
{
  initSta();
  auto timin_graph = openSta_->graph();
  auto block = db_->getChip()->getBlock();

  populateRoutingGraphs();

  candidate_paths_.clear();
  for(auto cell : block->getInsts()) {
    if(cell->isBlock() || cell->isFixed() || isClockBuffer(cell)) {
      continue;
    }
    TimeSubPath candidate_path = {.driver=nullptr, .sink=nullptr, .weight=0, .wl=0};
    for(auto iterm : cell->getITerms()) {
      if (!iterm->getNet() || (iterm->getNet()->getSigType() == odb::dbSigType::GROUND)
      || (iterm->getNet()->getSigType() == odb::dbSigType::POWER)
      || (iterm->getIoType() == odb::dbIoType::OUTPUT)) {
        continue;
      }
      odb::dbNet* pin_net = iterm->getNet();
      /* Don't move cells in high fanout nets
      if(pin_net->getITermCount() + pin_net->getBTermCount() > 4) {
        continue;
      }*/
      odb::dbITerm* driver = pin_net->getDrivingITerm(); 
      if(routing_grpahs_.find(pin_net) == routing_grpahs_.end()) {
        logger_->report("weird, found an unbuild net");
        continue;
      }
      int64_t wl = getWl2Sink(routing_grpahs_[pin_net], iterm);
      if(wl == -1) {
        logger_->report("Did not found pin {} on graph", iterm->getName());
        continue;
      }
      auto pin = network_->dbToSta(iterm);
      sta::Vertex * vertex = timin_graph->pinLoadVertex(pin);
      auto setup_slack = openSta_->slack(vertex, sta::MinMax::max());
      if(sta::delayLess(setup_slack, candidate_path.weight, openSta_)) {
        candidate_path.weight = setup_slack;
        candidate_path.wl = wl;
        candidate_path.driver = driver;
        candidate_path.sink = iterm;
      }
    }
    if(candidate_path.weight < 0.0) {
      candidate_paths_[candidate_path.weight].insert(candidate_path);
    }
  }
}

void CellMoveRouter::reportCandidateCells(const char * file_name)
{
  findCandidatePaths();
  std::ofstream outFile(file_name);  // Creates/opens file
  if (!outFile) {
      std::cerr << "Error opening file!" << std::endl;
  }

  outFile << "Design has " << candidate_paths_.size() << " cells with negative slacks" << std::endl;
  outFile << "-------- Top 10 worst slacks an it's 10 cells with  longest wl--------" << std::endl;

  int cont_slack = 0;
  for(auto [slack, cells] : candidate_paths_) {
    if(cont_slack == 20) {
      break;
    }
    outFile << slack << " {";
    int cont = 0;
    for(auto cell = cells.rbegin(); cell != cells.rend(); cell = std::next(cell)) {
      if(cont == 10) {
        break;
      }
      outFile  << cell->sink->getName() << " : " << cell->wl << ", ";
      ++cont;
    }
    outFile << "}\n";
    ++cont_slack;
  }
  outFile.close();  // Always close when done
}

void CellMoveRouter::reportWl2SlackRatio(const char * file_name)
{
  initSta();
  std::ofstream outFile(file_name);  // Creates/opens file
  outGraphName_ = "testGraphGcd";
  if (!outFile) {
      std::cerr << "Error opening file!" << std::endl;
  }

  auto block = db_->getChip()->getBlock();
  openSta_->ensureGraph();
  openSta_->searchPreamble();
  for (auto mode : openSta_->modes()) {
    openSta_->ensureClkNetwork(mode);
  }
  auto timin_graph = openSta_->graph();

  populateRoutingGraphs();

  std::vector<RcmCell> cells_weigth;
  for(auto cell : block->getInsts()) {
    outFile << "Cell " << cell->getName() << ":" <<std::endl;
    if(isClockBuffer(cell)) {
      continue;
    }
    RcmCell cell_weight = {.inst=cell, .iterm=nullptr, .weight=0, .distance_to_mediana=0, .init_stwl=0, .mediana={0,0}};
    for(auto iterm : cell->getITerms()) {
      if (!iterm->getNet() || (iterm->getNet()->getSigType() == odb::dbSigType::GROUND)
      || (iterm->getNet()->getSigType() == odb::dbSigType::POWER)
      || (iterm->getIoType() == odb::dbIoType::OUTPUT)) {
        continue;
      }
      odb::dbNet* pin_net = iterm->getNet();
      if(routing_grpahs_.find(pin_net) == routing_grpahs_.end()) {
        logger_->report("weird, found an unbuild net");
        continue;
      }
      int wl = getWl2Sink(routing_grpahs_[pin_net], iterm);
      auto net_wl = grt_->computeNetWirelength(pin_net);
      if(wl == -1) {
        logger_->report("Did not found pin {} on graph", iterm->getName());
        continue;
      }
      outFile << "  Pin " << iterm->getName() << ":" <<std::endl;
      auto pin = network_->dbToSta(iterm);
      sta::Vertex * vertex = timin_graph->pinLoadVertex(pin);
      auto setup_slack = openSta_->slack(vertex, sta::MinMax::max());
      outFile << "    setup slack = " << setup_slack << std::endl;
      outFile << "    wl = " << wl << std::endl;
      if(wl >= 400000 && setup_slack < 0.0) {
        outFile << "    Aquiiiiiiiii" << std::endl;
      }
      //outFile << "    wl2slack ratio = " << ((double) wl) / setup_slack << std::endl;
      outFile << "    net wl = " << net_wl << std::endl;
      double weigth = (double) wl;
      cell_weight.weight = std::max(weigth, cell_weight.weight);
    }
    outFile << "--------------- = " << std::endl;
    cells_weigth.push_back(cell_weight);
  }
  writeGraphToFile(routing_grpahs_, outGraphName_);
  outFile.close();  // Always close when done

  std::ofstream outFile2("cells weigths report.txt");  // Creates/opens file
  if (!outFile2) {
      std::cerr << "Error opening file 2!" << std::endl;
  }
  outFile2 << "-------- Top 30 weigths --------" << std::endl;

  std::sort(cells_weigth.begin(),cells_weigth.end(),
            [](const RcmCell a, const RcmCell b) {
                return a.weight > b.weight;
            });
  int cont = 0;
  for(RcmCell cell : cells_weigth) {
    if(cont == 30) {
      break;
    }
    outFile2 << cont + 1 << " - " << cell.inst->getName() << " : " << cell.weight << std::endl;
    cont += 1;
  }
  outFile2.close();  // Always close when done
}

void CellMoveRouter::moveSpecificCell()
{
  auto block = db_->getChip()->getBlock();
  odb::dbInst* cell2move = nullptr;
  odb::dbITerm* cell2move_pin = nullptr;
  odb::dbInst* cell2move2 = nullptr;
  odb::dbITerm* cell2move2_pin = nullptr;
  odb::dbInst* cell2move3 = nullptr;
  odb::dbITerm* cell2move3_pin = nullptr;

  for(odb::dbInst* inst : block->getInsts()) {
    if(inst->getName() == "_21316_") {
      cell2move = inst;
      for(auto term : inst->getITerms()) {
        if(term->getName() == "_21316_/A2") {
          cell2move_pin = term;
          break;
        }
      }
      continue;
    }
    if(inst->getName() == "place2396") {
      cell2move2 = inst;
      for(auto term : inst->getITerms()) {
        if(term->getName() == "place2396/A") {
          cell2move2_pin = term;
          break;
        }
      }
      continue;
    }
    if(inst->getName() == "_25605_") {
      cell2move3 = inst;
      for(auto term : inst->getITerms()) {
        if(term->getName() == "_25605_/B1") {
          cell2move3_pin = term;
          break;
        }
      }
      break;
    }
  }
  if(cell2move == nullptr) {
    logger_->report("Cell _21316_ not found");
    return;
  }
  if (cell2move_pin == nullptr) {
    logger_->report("Cell pin _21316_/A2 not found");
    return;
  }
  int64_t original_wl = std::numeric_limits<int64_t>::max();
  std::optional<std::pair<odb::Point, int64_t>> target_location = findSinkTargetPosition(cell2move, cell2move_pin, original_wl);
  // Proposed movement = (306850, 308000)
  logger_->report("Median for cell {} = ({}, {})",
                  cell2move->getName(),
                  target_location->first.x(),
                  target_location->first.y());
  cell2move->setLocation(target_location->first.x(), target_location->first.y());
  if(cell2move2 == nullptr) {
    logger_->report("Cell place2396 not found");
    return;
  }
  if(cell2move2_pin == nullptr) {
    logger_->report("Cell pin place2396/A not found");
    return;
  }
  
  std::optional<std::pair<odb::Point, int64_t>> target_location2 = findSinkTargetPosition(cell2move2, cell2move2_pin, original_wl);
  logger_->report("Median for cell {} = ({}, {})",
                  cell2move2->getName(),
                  target_location2->first.x(),
                  target_location2->first.y());
  cell2move2->setLocation(target_location2->first.x(), target_location2->first.y());

  if(cell2move3 == nullptr) {
    logger_->report("Cell _25605_ not found");
    return;
  }
  if(cell2move3_pin == nullptr) {
    logger_->report("Cell pin _25605_/B1 not found");
    return;
  }
  std::optional<std::pair<odb::Point, int64_t>> target_location3 = findSinkTargetPosition(cell2move3, cell2move3_pin, original_wl);
  // Proposed movement 438520, 260400
  logger_->report("Median for cell {} = ({}, {})",
                  cell2move3->getName(),
                  target_location3->first.x(),
                  target_location3->first.y());
  cell2move3->setLocation(target_location3->first.x(), target_location3->first.y());
}

std::optional<std::pair<odb::Point, int64_t>>
CellMoveRouter::findSinkTargetPosition(odb::dbInst* cell, odb::dbITerm* input_pin, int64_t original_wl)
{
  if(cell->isBlock() || cell->isFixed()) {
    return std::nullopt;
  }
  odb::dbITerm* output_pin = cell->getFirstOutput();
  if(output_pin == nullptr) {
    return std::nullopt;
  }
  odb::dbNet* output_net = output_pin->getNet();
  odb::dbNet* input_net = input_pin->getNet();
  
  int output_xl = std::numeric_limits<int>::max();
  int output_yl = std::numeric_limits<int>::max();
  int output_xr = std::numeric_limits<int>::min();
  int output_yu = std::numeric_limits<int>::min();
  for(auto iterm : output_net->getITerms()) {
    int x, y;
    //Using Cell location (fast)
    odb::dbInst* inst = iterm->getInst();
    if(inst && inst != cell) // is connected
    {
      inst->getLocation(x, y);
      output_xr = std::max(output_xr, x);
      output_yu = std::max(output_yu, y);
      output_xl = std::min(output_xl, x);
      output_yl = std::min(output_yl, y);
    }
  }

  for (auto bterm : output_net->getBTerms()) {
    int x=0, y=0;
    const bool pinExist = bterm->getFirstPinLocation(x, y);

    if(pinExist) {
      output_xr = std::max(output_xr, x);
      output_yu = std::max(output_yu, y);
      output_xl = std::min(output_xl, x);
      output_yl = std::min(output_yl, y);
    }
  }

  int input_driver_x;
  int input_driver_y;
  for(auto iterm : input_net->getITerms()) {
    if(iterm && iterm->getIoType() == odb::dbIoType::OUTPUT) {
      odb::dbInst* inst = iterm->getInst();
      inst->getLocation(input_driver_x, input_driver_y);
    }
  }

  for (auto bterm : input_net->getBTerms()) {
    int x=0, y=0;
    const bool pinExist = bterm->getFirstPinLocation(x, y);

    if(pinExist && bterm->getIoType() == odb::dbIoType::OUTPUT) {
      input_driver_x = x;
      input_driver_y = y;
    }
  }

  odb::Point output_ll = odb::Point(output_xl, output_yl);
  odb::Point output_ur = odb::Point(output_xr, output_yu);
  odb::Rect output_rect = odb::Rect(output_ll, output_ur);

  std::optional<std::pair<odb::Point, int64_t>> best_movement;
  odb::Point input_driver_pos = odb::Point(input_driver_x, input_driver_y);
  if(output_rect.intersects(input_driver_pos)) {
    best_movement = std::make_pair(input_driver_pos, int64_t{0});
    return best_movement;
  }

  int64_t best_dist = original_wl;
  for(auto output_point : output_rect.getPoints()) {
    int64_t dist = odb::Point::manhattanDistance(output_point, input_driver_pos);
    if(dist < best_dist) {
      best_dist = dist;
      best_movement = std::make_pair(output_point, dist);
    }
  }

  return best_movement;
}

std::optional<std::pair<odb::Point, int64_t>>
CellMoveRouter::findSourceTargetPosition(odb::dbITerm* output_pin, int64_t original_wl)
{
  if(output_pin == nullptr ) {
    return std::nullopt;
  }

  odb::dbInst* source_cell = output_pin->getInst();
  if(source_cell->isBlock() || source_cell->isFixed()) {
    return std::nullopt;
  }

  odb::dbNet* output_net = output_pin->getNet();
  odb::dbNet* input_net = nullptr;
  sta::Graph* timin_graph = openSta_->graph();
  sta::Slack worst_slack = 0;
  for(odb::dbITerm* source_term : source_cell->getITerms()) {
    if(source_term->getIoType() == odb::dbIoType::OUTPUT) {
      continue;
    }
    auto pin = network_->dbToSta(source_term);
    sta::Vertex * vertex = timin_graph->pinLoadVertex(pin);
    auto setup_slack = openSta_->slack(vertex, sta::MinMax::max());
    if(sta::delayLess(setup_slack, worst_slack, openSta_)) {
      worst_slack = setup_slack;
      input_net = source_term->getNet();
    }
  }

  if(input_net == nullptr) {
    return std::nullopt;
  }
  
  int output_xl = std::numeric_limits<int>::max();
  int output_yl = std::numeric_limits<int>::max();
  int output_xr = std::numeric_limits<int>::min();
  int output_yu = std::numeric_limits<int>::min();
  for(auto iterm : output_net->getITerms()) {
    int x, y;
    //Using Cell location (fast)
    odb::dbInst* inst = iterm->getInst();
    if(inst && inst != source_cell) // is connected
    {
      inst->getLocation(x, y);
      output_xr = std::max(output_xr, x);
      output_yu = std::max(output_yu, y);
      output_xl = std::min(output_xl, x);
      output_yl = std::min(output_yl, y);
    }
  }

  for (auto bterm : output_net->getBTerms()) {
    int x=0, y=0;
    const bool pinExist = bterm->getFirstPinLocation(x, y);

    if(pinExist) {
      output_xr = std::max(output_xr, x);
      output_yu = std::max(output_yu, y);
      output_xl = std::min(output_xl, x);
      output_yl = std::min(output_yl, y);
    }
  }

  int input_xl = std::numeric_limits<int>::max();
  int input_yl = std::numeric_limits<int>::max();
  int input_xr = std::numeric_limits<int>::min();
  int input_yu = std::numeric_limits<int>::min();
  for(auto iterm : input_net->getITerms()) {
    int x, y;
    //Using Cell location (fast)
    odb::dbInst* inst = iterm->getInst();
    if(inst && inst != source_cell) // is connected
    {
      inst->getLocation(x, y);
      input_xr = std::max(input_xr, x);
      input_yu = std::max(input_yu, y);
      input_xl = std::min(input_xl, x);
      input_yl = std::min(input_yl, y);
    }
  }

  for (auto bterm : input_net->getBTerms()) {
    int x=0, y=0;
    const bool pinExist = bterm->getFirstPinLocation(x, y);

    if(pinExist) {
      input_xr = std::max(input_xr, x);
      input_yu = std::max(input_yu, y);
      input_xl = std::min(input_xl, x);
      input_yl = std::min(input_yl, y);
    }
  }

  odb::Point output_ll = odb::Point(output_xl, output_yl);
  odb::Point output_ur = odb::Point(output_xr, output_yu);
  odb::Rect output_rect = odb::Rect(output_ll, output_ur);

  odb::Point input_ll = odb::Point(input_xl, input_yl);
  odb::Point input_ur = odb::Point(input_xr, input_yu);
  odb::Rect input_rect = odb::Rect(input_ll, input_ur);

  std::optional<std::pair<odb::Point, int64_t>> best_movement;
  if(output_rect.intersects(input_rect)) {
    odb::Rect result;
    output_rect.intersection(input_rect, result);
    best_movement = std::make_pair(result.center(), int64_t{0});
    return best_movement;
  }

  int64_t best_dist = original_wl;
  for(auto input_point : input_rect.getPoints()) {
    for(auto output_point : output_rect.getPoints()) {
      int64_t dist = odb::Point::manhattanDistance(output_point, input_point);
      if(dist < best_dist) {
        best_dist = dist;
        best_movement = std::make_pair(output_point, dist);
      }
    }
  }

  return best_movement;
}

void CellMoveRouter::InitCellsWeight() {
  cells_to_move_.clear();
  for(auto [slack, paths] : candidate_paths_) {
    for(auto path = paths.rbegin(); path != paths.rend(); path = std::next(path)) {
      std::optional<std::pair<odb::Point, int64_t>> sinkMove = findSinkTargetPosition(path->sink->getInst(), path->sink, path->wl);
      std::optional<std::pair<odb::Point, int64_t>> sourceMove = findSourceTargetPosition(path->driver, path->wl);
      RcmCell cell_movement = {.inst=nullptr, .iterm=nullptr, .weight=0, .distance_to_mediana=std::numeric_limits<int64_t>::max(), .init_stwl=path->wl, .mediana={0,0}};
      bool push_move = false;
      if(sinkMove.has_value()) {
        cell_movement.inst = path->sink->getInst();
        cell_movement.iterm = path->sink;
        cell_movement.distance_to_mediana = sinkMove->second;
        cell_movement.mediana = sinkMove->first;
        push_move = true;
      }
      if(sourceMove.has_value() && sourceMove->second < cell_movement.distance_to_mediana) {
        cell_movement.distance_to_mediana = sourceMove->second;
        cell_movement.mediana = sourceMove->first;
        cell_movement.iterm = path->driver;
        cell_movement.inst = path->driver->getInst();
        push_move = true;
      }
      if(push_move) {
        cells_to_move_.push_back(cell_movement);
      }
    }
  }
}

void CellMoveRouter::InitCellsWeightRand() {
  cells_to_move_.clear();
  std::vector<RcmCell> temp;
  for(auto [slack, paths] : candidate_paths_) {
    for(auto path = paths.rbegin(); path != paths.rend(); path = std::next(path)) {
      std::optional<std::pair<odb::Point, int64_t>> sinkMove = findSinkTargetPosition(path->sink->getInst(), path->sink, path->wl);
      std::optional<std::pair<odb::Point, int64_t>> sourceMove = findSourceTargetPosition(path->driver, path->wl);
      RcmCell cell_movement = {.inst=nullptr, .iterm=nullptr, .weight=0, .distance_to_mediana=std::numeric_limits<int64_t>::max(), .init_stwl=path->wl, .mediana={0,0}};
      bool push_move = false;
      if(sinkMove.has_value()) {
        cell_movement.inst = path->sink->getInst();
        cell_movement.iterm = path->sink;
        cell_movement.distance_to_mediana = sinkMove->second;
        cell_movement.mediana = sinkMove->first;
        push_move = true;
      }
      if(sourceMove.has_value() && sourceMove->second < cell_movement.distance_to_mediana) {
        cell_movement.distance_to_mediana = sourceMove->second;
        cell_movement.mediana = sourceMove->first;
        cell_movement.iterm = path->driver;
        cell_movement.inst = path->driver->getInst();
        push_move = true;
      }      
      if(push_move) {
        temp.push_back(cell_movement);
      }
    }
  }
  RcmCell first_pre_rand = temp[0];
  std::random_device rd;                 // non-deterministic seed source
  std::mt19937 gen(rd());                // Mersenne Twister engine

  std::shuffle(temp.begin(), temp.end(), gen);
  cells_to_move_.assign(temp.begin(), temp.end());
  logger_->report("temp size = {}, cell to move size = {}", temp.size(), cells_to_move_.size());
  logger_->report("first cell pre rand = {}, cell to move size = {}", first_pre_rand.inst->getName(), cells_to_move_[0].inst->getName());
}

void CellMoveRouter::InitCellsWeightWl() {
  cells_to_move_.clear();
  for(auto [slack, paths] : candidate_paths_) {
    for(auto path = paths.rbegin(); path != paths.rend(); path = std::next(path)) {
      std::optional<std::pair<odb::Point, int64_t>> sinkMove = findSinkTargetPosition(path->sink->getInst(), path->sink, path->wl);
      std::optional<std::pair<odb::Point, int64_t>> sourceMove = findSourceTargetPosition(path->driver, path->wl);
      RcmCell cell_movement = {.inst=nullptr, .iterm=nullptr, .weight=0, .distance_to_mediana=std::numeric_limits<int64_t>::max(), .init_stwl=path->wl, .mediana={0,0}};
      bool push_move = false;
      if(sinkMove.has_value()) {
        cell_movement.inst = path->sink->getInst();
        cell_movement.iterm = path->sink;
        cell_movement.distance_to_mediana = sinkMove->second;
        cell_movement.mediana = sinkMove->first;
        push_move = true;
      }
      if(sourceMove.has_value() && sourceMove->second < cell_movement.distance_to_mediana) {
        cell_movement.distance_to_mediana = sourceMove->second;
        cell_movement.mediana = sourceMove->first;
        cell_movement.iterm = path->driver;
        cell_movement.inst = path->driver->getInst();
        push_move = true;
      }      
      if(push_move) {
        cells_to_move_.push_back(cell_movement);
      }
    }
  }

  // Order by path wl
  std::sort(cells_to_move_.begin(),cells_to_move_.end(),
            [](const RcmCell a, const RcmCell b) {
                return a.init_stwl > b.init_stwl;
            });
}

void CellMoveRouter::InitCellsWeightSlack() {
  cells_to_move_.clear();
  for(auto [slack, paths] : candidate_paths_) {
    for(auto path = paths.rbegin(); path != paths.rend(); path = std::next(path)) {
      std::optional<std::pair<odb::Point, int64_t>> sinkMove = findSinkTargetPosition(path->sink->getInst(), path->sink, path->wl);
      std::optional<std::pair<odb::Point, int64_t>> sourceMove = findSourceTargetPosition(path->driver, path->wl);
      RcmCell cell_movement = {.inst=nullptr, .iterm=nullptr, .weight=0, .distance_to_mediana=std::numeric_limits<int64_t>::max(), .init_stwl=path->wl, .mediana={0,0}};
      bool push_move = false;
      if(sinkMove.has_value()) {
        cell_movement.inst = path->sink->getInst();
        cell_movement.iterm = path->sink;
        cell_movement.distance_to_mediana = sinkMove->second;
        cell_movement.mediana = sinkMove->first;
        push_move = true;
      }
      if(sourceMove.has_value() && sourceMove->second < cell_movement.distance_to_mediana) {
        cell_movement.distance_to_mediana = sourceMove->second;
        cell_movement.mediana = sourceMove->first;
        cell_movement.iterm = path->driver;
        cell_movement.inst = path->driver->getInst();
        push_move = true;
      }      
      if(push_move) {
        cells_to_move_.push_back(cell_movement);
      }
    }
  }
}

void
CellMoveRouter::InitCellTree(){
  //std::cout<<"Initializing Cell rtree..."<<std::endl;
  cellrTree_ = std::make_unique<CRTree>();

  auto block = db_->getChip()->getBlock();
  auto cells = block->getInsts();

  for (auto cell : cells) {
    auto lx = cell->getBBox()->xMin();
    auto rx = cell->getBBox()->xMax();
    auto ly = cell->getBBox()->yMin();
    auto uy = cell->getBBox()->yMax();

    box_t cell_box({lx, ly}, {rx, uy});
    CellElement el = std::pair(cell_box, cell);
    cellrTree_->insert(el);
  }
}

void
CellMoveRouter::InitGCellTree() {
  //std::cout<<"Initializing GCell rtree..."<<std::endl;
  gcellTree_ = std::make_unique<GRTree>();

  auto block = db_->getChip()->getBlock();
  auto ggrid = block->getGCellGrid();

  ggrid_max_x_ = block->getDieArea().xMax();
  ggrid_min_x_ = block->getDieArea().xMin();
  ggrid_max_y_ = block->getDieArea().yMax();
  ggrid_min_y_ = block->getDieArea().yMin();
  std::vector<int> gridX, gridY;
  ggrid->getGridX(gridX);
  ggrid->getGridY(gridY);
  gridX.push_back(ggrid_max_x_);
  gridY.push_back(ggrid_max_y_);


  auto prev_y = *gridY.begin();
  auto prev_x = *gridX.begin();

  for(auto y_it = std::next(gridY.begin()); y_it != gridY.end(); y_it++)
  {
    int yll = prev_y;
    int yur = *y_it;
    for(auto x_it = std::next(gridX.begin()); x_it != gridX.end(); x_it++)
    {
      int xll = prev_x;
      int xur = *x_it;
      box_t gcell_box({xll, yll}, {xur, yur});

      odb::Rect Bbox = odb::Rect(xll, yll, xur, yur);
      //rectangleRender_->addRectangle(Bbox);
      GCellElement el = std::pair(gcell_box, Bbox);
      //rectangleRender_->addRectangle(Bbox);
      gcellTree_->insert(el);
      prev_x = *x_it;
    }
    prev_x = *gridX.begin();
    prev_y = *y_it;
  }
}

void CellMoveRouter::initSta()
{
  openSta_->ensureGraph();
  openSta_->searchPreamble();
  for (auto mode : openSta_->modes()) {
    openSta_->ensureClkNetwork(mode);
  }
  worst_slack_ = openSta_->worstSlack(sta::MinMax::max());
}

void
CellMoveRouter::Cell_Move_Rerout(){

  auto grcmo_start = std::chrono::high_resolution_clock::now();
  std::unordered_map<odb::dbInst*,int> cells_movement;



  // Inital Global Rout by OpenROAD
  // grt_->globalRoute();
  grt_->setVerbose(false); 
  long init_wl = grt_->computeWirelength();
  std::cout<<"initial wl  "<<init_wl<<std::endl;
  std::cout<<"initial #vias  "<<grt_->getViaCount()<<std::endl;
  std::cout<<"Design has "<<db_->getChip()->getBlock()->getNets().size()<<" nets"<<std::endl;
  gui::Gui* gui = gui::Gui::get();
  if (rectangleRender_ == nullptr)
  {
    rectangleRender_ = std::make_unique<RectangleRender>();
    gui->registerRenderer(rectangleRender_.get());
  }

  //Initalize Rtrees
  InitCellTree();
  InitGCellTree();
  abacus_.InitRowTree();

  int total_moved = 0;
  int total_rejected = 0;
  int total_worse = 0;
  int iterations = 0;
  int n_move_cells = 0;
  for(int j = 0; j < 20; j++) {
    auto itt_start = std::chrono::high_resolution_clock::now();
    initSta();
    InitCellsWeight();
    std::cout<<"Celulas a serem movidas  "<<cells_to_move_.size()<<std::endl;
    int success = 0;
    int rejected = 0;
    int failed = 0;
    int worse = 0;
    while(!cells_to_move_.empty()) {
      auto moving_cell = cells_to_move_[0].inst;
      bool complete = true; //Swap_and_Rerout(moving_cell, failed, worse, cells_to_move_[0].init_stwl);
      if(complete) {
        cells_movement[moving_cell] += 1;
        success++;
      } else {
        rejected++;
        cells_to_move_.erase(cells_to_move_.begin());
      }
    }

    total_moved += success;
    total_rejected += rejected;
    total_worse += worse;
    iterations ++;
    auto itt_end = std::chrono::high_resolution_clock::now();
    auto itt_duration = std::chrono::duration_cast<std::chrono::milliseconds>(itt_end - itt_start);
    long after_wl = grt_->computeWirelength();
    std::cout<<"iteração: "<<iterations<<std::endl;
    std::cout<<" wl (um): "<<after_wl<<std::endl;
    std::cout<<" #vias: "<<grt_->getViaCount()<<std::endl;
    std::cout<<" movimentos: "<<success<<std::endl;
    std::cout<<" movidas: "<< cells_movement.size()<<std::endl;
    std::cout<<" rejeitadas: "<<rejected<<std::endl;
    std::cout<<" worse: "<<worse<<std::endl;
    std::cout<<" duration (ms): "<<itt_duration.count()<<std::endl;
    if(success == 0) {
      break;
    }

  }

  std::vector<odb::dbNet*> nets;
  nets.reserve(db_->getChip()->getBlock()->getNets().size());
  for (odb::dbNet* db_net : db_->getChip()->getBlock()->getNets()) {
    nets.push_back(db_net);
  }
  grt_->saveGuides(nets);
  auto grcmo_end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::seconds>(grcmo_end - grcmo_start);

  std::cout<<"\ntotal moved cells  "<<total_moved<<std::endl;
  std::cout<<"total rejected cells  "<<total_rejected<<std::endl;
  std::cout<<"total move worsed cells  "<<total_worse<<std::endl;
  long after_wl = grt_->computeWirelength();
  std::cout<<"final wl  "<<after_wl<<std::endl;
  std::cout<<"final #vias  "<<grt_->getViaCount()<<std::endl;
  std::cout<<"iterations  "<<iterations<<std::endl;
  std::cout<<"total duration (ms): "<<duration.count()<<std::endl;

  /*for(auto [inst, count] : cells_movement) {
    std::cout<<"Inst "<<inst->getName()<<" moved: "<<count<<" times"<< std::endl;
  }*/
  std::cout<<"Effectvly moved: "<< cells_movement.size()<<std::endl;
}

void
CellMoveRouter::RcmTimeMove(){
  auto grcmo_start = std::chrono::high_resolution_clock::now();
  std::unordered_map<odb::dbInst*,int> cells_movement;
  //Initalize Rtrees
  initSta();
  InitCellTree();
  InitGCellTree();
  abacus_.InitRowTree();
  grt_->setVerbose(false);

  int64_t init_wl = grt_->computeWirelength();
  int64_t init_via_count = grt_->getViaCount();
  sta::Slack init_tns = openSta_->totalNegativeSlack(sta::MinMax::max());
  std::cout<<"Design has " << db_->getChip()->getBlock()->getNets().size() << " nets"<<std::endl;
  std::cout<<"initial wl  " << init_wl << std::endl;
  std::cout<<"initial #vias  " << init_via_count << std::endl;
  std::cout<<"initial worst slack  " << worst_slack_ << std::endl;
  std::cout<<"initial total negative slack  " << init_tns << std::endl;
  gui::Gui* gui = gui::Gui::get();
  if (rectangleRender_ == nullptr)
  {
    rectangleRender_ = std::make_unique<RectangleRender>();
    gui->registerRenderer(rectangleRender_.get());
  }

  findCandidatePaths();
  InitCellsWeight();
  int success = 0;
  int rejected = 0;
  int failed = 0;
  int worse = 0;
  std::cout<<"# Celulas candidatas  "<<cells_to_move_.size()<<std::endl;
  while(!cells_to_move_.empty()) {
    odb::dbInst* moving_cell = cells_to_move_[0].inst;
    bool complete = moveRerout(cells_to_move_[0]);
    if(complete) {
      cells_movement[moving_cell] += 1;
      success++;
    } else {
      rejected++;
      cells_to_move_.erase(cells_to_move_.begin());
    }

    /*if(!(success % 10)) {
      estimate_parasitics_->estimateParasitics(est::ParasiticsSrc::global_routing);
      initSta();
      std::cout<<"Iteration: "<<success<<"\n";
      std::cout<<"  worst slack  "<<openSta_->worstSlack(sta::MinMax::max())<<std::endl;
      std::cout<<"  total negative slack  "<<openSta_->totalNegativeSlack(sta::MinMax::max())<<std::endl;
      // update timing and report
    }*/
  }

  std::vector<odb::dbNet*> nets;
  nets.reserve(db_->getChip()->getBlock()->getNets().size());
  for (odb::dbNet* db_net : db_->getChip()->getBlock()->getNets()) {
    nets.push_back(db_net);
  }
  grt_->saveGuides(nets);
  auto grcmo_end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::seconds>(grcmo_end - grcmo_start);
  estimate_parasitics_->estimateParasitics(est::ParasiticsSrc::global_routing);
  std::cout<<"\ntotal moved cells  "<<success<<std::endl;
  std::cout<<"total rejected cells  "<<rejected<<std::endl;
  long after_wl = grt_->computeWirelength();
  std::cout<<"final wl  "<<after_wl<<std::endl;
  std::cout<<"final #vias  "<<grt_->getViaCount()<<std::endl;
  std::cout<<"final worst slack  "<<openSta_->worstSlack(sta::MinMax::max())<<std::endl;
  std::cout<<"final total negative slack  "<<openSta_->totalNegativeSlack(sta::MinMax::max())<<std::endl;
  std::cout<<"total duration (s): "<<duration.count()<<std::endl;
}

bool CellMoveRouter::moveRerout(RcmCell cell_to_move) {
  grt_->startIncremental();
  odb::dbInst* moving_cell = cell_to_move.inst;
  //Move cell and call abacus for legalization area
  if(moving_cell == nullptr) {
    return false;
  }
  moving_cell->setLocation(cell_to_move.mediana.x(), cell_to_move.mediana.y());
  grt_->endIncremental();
  for(auto pin : moving_cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL){
      if (net->getSigType().isSupply()) {
        continue;
      }
      for (auto iterm : net->getITerms()) {
        auto inst = iterm->getInst();
        
        //std::cout<<"Celula sendo procurada: "<<inst->getName()<<std::endl;
        auto erase = findInstIterator(inst);
        if(erase != cells_to_move_.end()) {
          cells_to_move_.erase(erase);
        }
      }
    }
  }
  return true;
}

std::vector<RcmCell>::iterator
CellMoveRouter::findInstIterator(const odb::dbInst* inst) {
  std::vector<RcmCell>::iterator iterator;
  for (iterator = cells_to_move_.begin(); iterator != cells_to_move_.end();) {
    odb::dbInst* inst_check = iterator->inst;
    if(inst_check == inst) {
      //std::cout<<"Celula achada: "<<inst_check->getName()<<std::endl;
      return iterator;
    }
    ++iterator;
  }
  return iterator;
}

std::vector<RcmCell>::iterator
CellMoveRouter::findInstIteratorWeight(const odb::dbInst* inst) {
  std::vector<RcmCell>::iterator iterator;
  for (iterator = cells_weight_.begin(); iterator != cells_weight_.end();) {
    odb::dbInst* inst_check = iterator->inst;
    if(inst_check == inst) {
      //std::cout<<"Celula achada: "<<inst_check->getName()<<std::endl;
      return iterator;
    }
    ++iterator;
  }
  return iterator;
}


bool CellMoveRouter::isClockBuffer(odb::dbInst* cell)
{
  auto outTerm = cell->getFirstOutput();
  if(!outTerm || !outTerm->getNet()) {
    return false;
  }
  bool isClock = outTerm->getNet()->getSigType() == odb::dbSigType::CLOCK;
  return isClock;
}

bool CellMoveRouter::isSequential(odb::dbInst* cell)
{
  
  sta::Cell* masterCell = network_->dbToSta(cell->getMaster());
  sta::LibertyCell* libertyCell = network_->libertyCell(masterCell);

  if (!libertyCell) {
    return false;
  }
  // Combinational components
  if (libertyCell->hasSequentials()) {
    return true;
  }

  return false;
}

void CellMoveRouter::runAbacus() { 
  auto block = db_->getChip()->getBlock();
  odb::Rect area = block->getCoreArea();
  abacus_.abacus(area.xMin(), area.yMin(), area.xMax(), area.yMax());
};

}
