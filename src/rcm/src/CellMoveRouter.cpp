#include "rcm/CellMoveRouter.h"
#include "grt/PinGridLocation.h"
#include "odb/db.h"
#include "gui/gui.h"
#include "odb/dbObject.h"
#include "odb/dbTypes.h"
#include "odb/geom.h"
#include "ord/OpenRoad.hh"
#include "sta/Delay.hh"
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
#include <ostream>
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
  network_{ord::OpenRoad::openRoad()->getDbNetwork()}
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
  std::stack<std::pair<Node, int>> stack;

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
    if(isClockBuffer(cell)) {
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
      odb::dbITerm* driver = pin_net->getDrivingITerm(); 
      if(routing_grpahs_.find(pin_net) == routing_grpahs_.end()) {
        logger_->report("weird, found an unbuild net");
        continue;
      }
      int wl = getWl2Sink(routing_grpahs_[pin_net], iterm);
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
      double weigth = (double) wl * (double) slackWeigthExp(iterm);
      if(cell->getName() == "_21316_") {
        logger_->report("CEll _21316_ slack weigth = {}", slackWeigthExp(iterm));
        logger_->report("CEll _21316_ slack slack = {}", setup_slack);
        logger_->report("Actual worst slack = {}", worst_slack_);
      }
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

  odb::Point target_location = findSinkTargetPosition(cell2move, cell2move_pin);
  // Proposed movement = (306850, 308000)
  logger_->report("Median for cell {} = ({}, {})",
                  cell2move->getName(),
                  target_location.x(),
                  target_location.y());
  cell2move->setLocation(target_location.x(), target_location.y());
  if(cell2move2 == nullptr) {
    logger_->report("Cell place2396 not found");
    return;
  }
  if(cell2move2_pin == nullptr) {
    logger_->report("Cell pin place2396/A not found");
    return;
  }
  
  target_location = findSinkTargetPosition(cell2move2, cell2move2_pin);
  logger_->report("Median for cell {} = ({}, {})",
                  cell2move2->getName(),
                  target_location.x(),
                  target_location.y());
  cell2move2->setLocation(target_location.x(), target_location.y());

  if(cell2move3 == nullptr) {
    logger_->report("Cell _25605_ not found");
    return;
  }
  if(cell2move3_pin == nullptr) {
    logger_->report("Cell pin _25605_/B1 not found");
    return;
  }
  target_location = findSinkTargetPosition(cell2move3, cell2move3_pin);
  // Proposed movement 438520, 260400
  logger_->report("Median for cell {} = ({}, {})",
                  cell2move3->getName(),
                  target_location.x(),
                  target_location.y());
  cell2move3->setLocation(target_location.x(), target_location.y());
}

odb::Point
CellMoveRouter::findSinkTargetPosition(odb::dbInst* cell, odb::dbITerm* input_pin)
{
  odb::dbITerm* output_pin = cell->getFirstOutput();
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

  int input_xl = std::numeric_limits<int>::max();
  int input_yl = std::numeric_limits<int>::max();
  int input_xr = std::numeric_limits<int>::min();
  int input_yu = std::numeric_limits<int>::min();
  for(auto iterm : input_net->getITerms()) {
    int x, y;
    //Using Cell location (fast)
    odb::dbInst* inst = iterm->getInst();
    if(inst && inst != cell) // is connected
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
  std::vector<odb::Point> output_bbox;
  odb::Point output_ll = odb::Point(output_xl, output_yl);
  output_bbox.push_back(output_ll);
  odb::Point output_ul = odb::Point(output_xl, output_yu);
  output_bbox.push_back(output_ul);
  odb::Point output_lr = odb::Point(output_xr, output_yl);
  output_bbox.push_back(output_lr);
  odb::Point output_ur = odb::Point(output_xr, output_yu);
  output_bbox.push_back(output_ul);

  std::vector<odb::Point> input_bbox;
  odb::Point input_ll = odb::Point(input_xl, input_yl);
  input_bbox.push_back(input_ll);
  odb::Point input_ul = odb::Point(input_xl, input_yu);
  input_bbox.push_back(input_ul);
  odb::Point input_lr = odb::Point(input_xr, input_yl);
  input_bbox.push_back(input_lr);
  odb::Point input_ur = odb::Point(input_xr, input_yu);
  input_bbox.push_back(input_ul);

  odb::Point best_movement;
  int64_t best_dist = std::numeric_limits<int64_t>::max();

  for(auto output_point : output_bbox) {
    for(auto input_point : input_bbox) {
      int64_t dist = odb::Point::manhattanDistance(output_point, input_point);
      if(dist < best_dist) {
        best_dist = dist;
        best_movement = output_point;
      }
    }
  }

  return best_movement;
}

odb::Point
CellMoveRouter::findSourceTargetPosition(odb::dbITerm* output_pin)
{
  sta::Graph* timin_graph = openSta_->graph();
  odb::dbInst* source_cell = output_pin->getInst();
  odb::dbNet* output_net = output_pin->getNet();
  odb::dbNet* input_net = nullptr;
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
  std::vector<odb::Point> output_bbox;
  odb::Point output_ll = odb::Point(output_xl, output_yl);
  output_bbox.push_back(output_ll);
  odb::Point output_ul = odb::Point(output_xl, output_yu);
  output_bbox.push_back(output_ul);
  odb::Point output_lr = odb::Point(output_xr, output_yl);
  output_bbox.push_back(output_lr);
  odb::Point output_ur = odb::Point(output_xr, output_yu);
  output_bbox.push_back(output_ul);

  std::vector<odb::Point> input_bbox;
  odb::Point input_ll = odb::Point(input_xl, input_yl);
  input_bbox.push_back(input_ll);
  odb::Point input_ul = odb::Point(input_xl, input_yu);
  input_bbox.push_back(input_ul);
  odb::Point input_lr = odb::Point(input_xr, input_yl);
  input_bbox.push_back(input_lr);
  odb::Point input_ur = odb::Point(input_xr, input_yu);
  input_bbox.push_back(input_ul);

  odb::Point best_movement;
  int64_t best_dist = std::numeric_limits<int64_t>::max();

  for(auto input_point : input_bbox) {
    for(auto output_point : output_bbox) {
      int64_t dist = odb::Point::manhattanDistance(output_point, input_point);
      if(dist < best_dist) {
        best_dist = dist;
        best_movement = input_point;
      }
    }
  }

  return best_movement;
}


stt::Tree
CellMoveRouter::buildSteinerTree(odb::dbNet * net)
{

  if ((net->getSigType() == odb::dbSigType::GROUND)
      || (net->getSigType() == odb::dbSigType::POWER)
      || (net->getITermCount() + net->getBTermCount() < 2)) {
    return stt::Tree{};
  }
  const odb::dbITerm* driver = net->getDrivingITerm();
  //std::cout<<"driver id: "<<driverID<<"\n";
  /*if(driverID == 0 || driverID == -1) {
    net->get1stSignalInput()
    std::cout<<"Net "<< net->getName()<<" without a driver"<<std::endl; //apagar
    return stt::Tree{}; //throw std::logic_error("Error, net without a driver (should we skip it?).");
  }*/

  // Get pin coords and driver
  std::vector<int> xcoords, ycoords;
  int rootIndex = 0;
  for(auto dbITerm : net->getITerms())
  {
    int x, y;
    const bool pinExist = dbITerm->getAvgXY(&x, &y);
    if(pinExist)
    {
      if(driver == dbITerm)
      {
        rootIndex = xcoords.size();
      }
      xcoords.push_back(x);
      ycoords.push_back(y);
    }
  }

  for(auto dbBTerm : net->getBTerms()) {
    int x, y;
    const bool pinExist = dbBTerm->getFirstPinLocation(x, y);
    if(pinExist)
    {
      if(dbBTerm->getIoType() == odb::dbIoType::INPUT)
      {
        rootIndex = xcoords.size();
      }
      xcoords.push_back(x);
      ycoords.push_back(y);
    }
  }
  //std::cout<<"root index: "<<rootIndex<< std::endl; //apagar
  if(rootIndex == -1){
    std::cout<<"NO ROOT INDEX ERROR"<< std::endl; //apagar
    return stt::Tree{};

  }
  // Build Steiner Tree
  const stt::Tree tree = stt_->makeSteinerTree(xcoords, ycoords, rootIndex);
  return tree;
}

stt::Tree
CellMoveRouter::buildSteinerTree(odb::dbNet * net, odb::dbITerm* movedTerm, int termX, int termY)
{

  if ((net->getSigType() == odb::dbSigType::GROUND)
      || (net->getSigType() == odb::dbSigType::POWER)
      || (net->getITermCount() + net->getBTermCount() < 2))
    return stt::Tree{};

  const odb::dbITerm* driver = net->getDrivingITerm();

  // Get pin coords and driver
  std::vector<int> xcoords, ycoords;
  int rootIndex = 0;
  for(auto dbITerm : net->getITerms())
  {
    int x, y;
    const bool pinExist = dbITerm->getAvgXY(&x, &y);
    if(pinExist && dbITerm != movedTerm)
    {
      if(driver == dbITerm)
      {
        rootIndex = xcoords.size();
      }
      xcoords.push_back(x);
      ycoords.push_back(y);
    } else if(dbITerm == movedTerm) {
      if(driver == dbITerm)
      {
        rootIndex = xcoords.size();
      }
      xcoords.push_back(termX);
      ycoords.push_back(termY);
    }
  }

  for(auto dbBTerm : net->getBTerms()) {
    int x, y;
    const bool pinExist = dbBTerm->getFirstPinLocation(x, y);
    if(pinExist)
    {
      if(dbBTerm->getIoType() == odb::dbIoType::INPUT)
      {
        rootIndex = xcoords.size();
      }
      xcoords.push_back(x);
      ycoords.push_back(y);
    }
  }
  //std::cout<<"root index: "<<rootIndex<< std::endl; //apagar
  if(rootIndex == -1){
    std::cout<<"NO ROOT INDEX ERROR"<< std::endl; //apagar
    return stt::Tree{};

  }
  // Build Steiner Tree
  const stt::Tree tree = stt_->makeSteinerTree(xcoords, ycoords, rootIndex);
  return tree;
}

int
CellMoveRouter::getTreeWl(const stt::Tree &tree) //get steiner wirelength from steiner tree object
{
  int treeWl = 0;
  
  for(int i = 0; i < tree.branchCount(); ++i)
  {
    const stt::Branch& branch = tree.branch[i];
    if(i == branch.n) {
      continue;
    }

    const int x1 = branch.x;
    const int y1 = branch.y;
    const stt::Branch& neighbor = tree.branch[branch.n];
    const int x2 = neighbor.x;
    const int y2 = neighbor.y;
    treeWl += abs(x1 - x2) + abs(y1 - y2);

  }
  return treeWl;
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
    if(limit_candidate_size_) {
      n_move_cells = std::floor(cells_weight_.size() * candidate_percentage_);
    }
    
    for(int i = cells_weight_.size() - 1; i >=0; i--) {
      if((limit_candidate_size_ && cells_to_move_.size() == n_move_cells) ||
         (!limit_candidate_size_ && cells_weight_[i].weight <= 0)) {
        break;
      }
      cells_to_move_.push_back(cells_weight_[i]);
    }
    std::cout<<"Celulas a serem movidas  "<<cells_to_move_.size()<<std::endl;
    int success = 0;
    int rejected = 0;
    int failed = 0;
    int worse = 0;
    while(!cells_to_move_.empty()) {
      auto moving_cell = cells_to_move_[0].inst;
      bool complete = Swap_and_Rerout(moving_cell, failed, worse, cells_to_move_[0].init_stwl);
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

bool
CellMoveRouter::Swap_and_Rerout(odb::dbInst * moving_cell,
                                int& failed_legalization,
                                int& worse_wl,
                                int before_estimate)
{
  std::map<odb::dbNet*, grt::GRoute>  affected_nets;
  std::vector<int>  nets_Bbox_Xs;
  std::vector<int>  nets_Bbox_Ys;
  int moving_cell_width = moving_cell->getBBox()->getDX();
  gui::Gui* gui = gui::Gui::get();
  //Finding the cell's nets bounding boxes
  int original_x, original_y;
  //logger_->report("moving cell: {}", moving_cell->getName()); 
  moving_cell->getLocation(original_x, original_y);

  for(auto pin : moving_cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL) {
      if (net->getSigType().isSupply()) {
        continue;
      }

      int xll = std::numeric_limits<int>::max();
      int yll = std::numeric_limits<int>::max();
      int xur = std::numeric_limits<int>::min();
      int yur = std::numeric_limits<int>::min();
      for(auto iterm : net->getITerms())
      {
        int x=0, y=0;
        //Using Cell location (fast)
        odb::dbInst* inst = iterm->getInst();
        if(inst && inst != moving_cell) // is connected
        {
          inst->getLocation(x, y);
          xur = std::max(xur, x);
          yur = std::max(yur, y);
          xll = std::min(xll, x);
          yll = std::min(yll, y);
        }
      }
      for (auto bterm : net->getBTerms()) {
        int x=0, y=0;
        const bool pinExist = bterm->getFirstPinLocation(x, y);

        if(pinExist) {
          //logger_->report("Net: {}", net->getName());
          xur = std::max(xur, x);
          yur = std::max(yur, y);
          xll = std::min(xll, x);
          yll = std::min(yll, y);
        }
      }
      nets_Bbox_Xs.push_back(xur);
      nets_Bbox_Xs.push_back(xll);
      nets_Bbox_Ys.push_back(yur);
      nets_Bbox_Ys.push_back(yll);
      //wl_before_moving += grt_->computeNetWirelength(net);
      grt::GRoute net_init_route = grt_->getNetRoute(net);
      affected_nets[net] = net_init_route;
    }
  }

  if(debug()) {
    int icx, icy;
    moving_cell->getLocation(icx, icy);
    std::cout<<"Cell to be moved: "<<moving_cell->getName()<<"\n";
    std::cout<<"  Intial Position: ("<<icx<<", "<<icy<<")"<<std::endl;
    std::cout<<"  Cell width: ("<<moving_cell_width<<std::endl;
  }

  //Get median cell Point
  //std::cout<<"Computing cell median point"<<std::endl;
  std::pair<int, int> Optimal_Region = nets_Bboxes_median(nets_Bbox_Xs, nets_Bbox_Ys);

  //move cell to median point
  int xll = ggrid_min_x_;
  int yll = ggrid_min_y_;
  int xur = ggrid_max_x_;
  int yur = ggrid_max_y_;

  if(debug()) {
    std::cout<<"  New Position: ("<<Optimal_Region.first<<", "<<Optimal_Region.second<<")"<<std::endl;
  }

  //Find median Gcell
  std::vector<GCellElement> result;
  gcellTree_->query(bgi::intersects(point_t(Optimal_Region.first, Optimal_Region.second)), std::back_inserter(result));
  
  if(debug()) {
    std::cout<<"Optimal Gcell: ("<<result[0].second.xMin()<<", "<<result[0].second.yMin()<<"), ";
    std::cout<<"("<<result[0].second.xMax()<<", "<<result[0].second.yMax()<<")\n";
    std::cout<<std::endl;
  }

  // Expend Legalization Area to be 10x10 GCells
  int gcell_height = result[0].second.yMax() - result[0].second.yMin();

  //Expanding legalization Area
  xur = std::min(xur, result[0].second.xMax() + (14 * gcell_height));
  yur = std::min(yur, result[0].second.yMax());
  xll = std::max(xll, result[0].second.xMin() - (14 * gcell_height));
  yll = std::max(yll, result[0].second.yMin());
  
  auto [best_x, best_y, has_enoght_space] = abacus_.get_free_spaces(moving_cell_width, xll, yll, xur, yur);
  
  if(!has_enoght_space) {
    return false;
  }

  if(best_x == original_x && best_y.yMin() == original_y) {
    return false;
  }

  int after_estimate = 0;
  for(auto pin : moving_cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL){
      if (net->getSigType().isSupply()) {
        continue;
      }
      int pinX, pinY;
      pin->getAvgXY(&pinX, &pinY);
      int moveX = best_x - original_x;
      int moveY = best_y.yMin() - original_y;
      auto tree = buildSteinerTree(net, pin, pinX + moveX, pinY + moveY); //make net steiner tree
      after_estimate += getTreeWl(tree);
    }
  }
  if(after_estimate > before_estimate) {
    return false;
  }

  grt_->startIncremental();

  //Move cell and call abacus for legalization area
  moving_cell->setLocation(best_x, best_y.yMin());
  auto changed_cells = abacus_.abacus(xll, best_y.yMin(), xur, best_y.yMax());

  if(debug()) {
    std::cout<<"Legalization area: ("<<xll<<", "<<yll<<")"<<"  ("<<xur<<", "<<yur<<")"<<std::endl;
    int icx, icy;
    moving_cell->getLocation(icx, icy);
    std::cout<<"Legalized Position: ("<<icx<<", "<<icy<<")"<<std::endl;
    std::cout<<"Number of moved cells by Abacus: "<<changed_cells.size()<<std::endl;
  }

  if(abacus_.failed()) {
    failed_legalization++;
    if(debug()) {
      rectangleRender_->addRectangle(odb::Rect(xll, best_y.yMin(), xur, best_y.yMax()));
      std::cout<<"Legalization area: ("<<xll<<", "<<best_y.yMin()<<")"<<"  ("<<xur<<", "<<best_y.yMax()<<")"<<std::endl;
      std::cout<<"Legalization area: ("<<xll<<", "<<yll<<")"<<"  ("<<xur<<", "<<yur<<")"<<std::endl;
    }
  }

  for(auto [cell, original_location] : changed_cells) {
    if(cell == moving_cell) {
      continue;
    }

    for(auto pin : cell->getITerms())
    {
      auto affected_net = pin->getNet();
      if(affected_net != NULL){
        if (affected_net->getSigType().isSupply() || affected_net->isSpecial()) {
          continue;
        }
        grt::GRoute net_init_route = grt_->getNetRoute(affected_net);
        affected_nets[affected_net] = net_init_route;
      }
    }
  }

  grt_->endIncremental();

  /*std::cout<<"Reroteando nets afetadas....."<<std::endl;
  //clear dirty nets and update the new nets ot be rerouted
  std::vector<odb::dbNet*>rerouted_nets;
  grt_->clearDirtyNets();
  for (const auto& [affected_net, net_route] : affected_nets) {
    if(affected_net->getSigType().isSupply()) {
      logger_->report("Erro nas nets afetadas");
    }
    rerouted_nets.push_back(affected_net);
    grt_->addDirtyNet(affected_net);
  }

  icr_grt_->updateRoutes();
  if(!grt_->getDirtyNets().empty()) {
    grt_->clearDirtyNets();
  }*/

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

  /*int wl_after_moving = grt_->computeWirelength();
  if(wl_after_moving > wl_before_moving) {
    int curr_x, curr_y;
    moving_cell->getLocation(curr_x, curr_y);
    logger_->report("cell: {}", moving_cell->getName());
    logger_->report("original pos: ({}, {})", original_x, original_y);
    logger_->report("current pos: ({}, {})", curr_x, curr_y);

    moving_cell->setLocation(original_x, original_y);
    for (auto [inst, original_location] : changed_cells) {
      if(inst == moving_cell) {
        continue;
      }
      int x_atual, y_atual;
      inst->getLocation(x_atual, y_atual);
      if(x_atual == original_location.first && y_atual == original_location.second) {
        logger_->report("Inst não moveu: {}", inst->getName());
      }
      inst->setLocation(original_location.first, original_location.second);
    }
    std::cout<<"\nUsos iniciais:"<<std::endl;
    logger_->report("H 2d usages: {}", total_usages_antes.first.first);
    logger_->report("V 2d usages: {}", total_usages_antes.first.second);
    logger_->report("H 3d usages: {}", total_usages_antes.second.first);
    logger_->report("V 3d usages: {}", total_usages_antes.second.second);    
    std::cout<<"\nUsos antes do updateNets:"<<std::endl;
    auto total_usages_pre_remove = grt_->reportTotalUsages();
    logger_->report("H 2d usages: {}", total_usages_pre_remove.first.first);
    logger_->report("V 2d usages: {}", total_usages_pre_remove.first.second);
    logger_->report("H 3d usages: {}", total_usages_pre_remove.second.first);
    logger_->report("V 3d usages: {}", total_usages_pre_remove.second.second);
    auto nets_reroteadas = grt_->updateNetsIncr(rerouted_nets);
    std::cout<<"\nUsos depois do updateNets:"<<std::endl;
    auto total_usages_pos_remove = grt_->reportTotalUsages();
    logger_->report("H 2d usages: {}", total_usages_pos_remove.first.first);
    logger_->report("V 2d usages: {}", total_usages_pos_remove.first.second);
    logger_->report("H 3d usages: {}", total_usages_pos_remove.second.first);
    logger_->report("V 3d usages: {}", total_usages_pos_remove.second.second);
    logger_->report("Tamanho do retorno: {}",nets_reroteadas.size());
    // Atualizar as informações das nets com o incremental.
    for (auto affected_net : nets_reroteadas) {
      auto net_guide = affected_nets[affected_net]; 
      grt_->loadGuidesFromUser(affected_net, net_guide);
    }
    worse_wl += 1;
    std::cout<<"Usos dpeois:"<<std::endl;
    auto total_usages_depois = grt_->reportTotalUsages();
    logger_->report("H 2d usages: {}", total_usages_depois.first.first);
    logger_->report("V 2d usages: {}", total_usages_depois.first.second);
    logger_->report("H 3d usages: {}", total_usages_depois.second.first);
    logger_->report("V 3d usages: {}\n", total_usages_depois.second.second);
    return false;
  }*/

  gui->redraw();
  //std::cout<<"nets afetadas reroteadas..."<<std::endl;
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

std::pair<int, int>
CellMoveRouter::nets_Bboxes_median(std::vector<int>& Xs, std::vector<int>& Ys) {

  if(Xs.size() == 0) {
    return median(0,0);
  }

  int median_pos_X = std::floor(Xs.size()/2);
  std::sort(Xs.begin(), Xs.end());
  Xs.erase(std::unique(Xs.begin(), Xs.end()), Xs.end());

  int median_pos_Y = std::floor(Ys.size()/2);
  std::sort(Ys.begin(), Ys.end());
  Ys.erase(std::unique(Ys.begin(), Ys.end()), Ys.end());

  if(Xs.size() == 1) {
    return median(Xs[median_pos_X], Ys[median_pos_Y]);
  }

  int x, y;
  if (Xs.size() % 2 == 0) {
    int xll = Xs[median_pos_X - 1];
    int xur = Xs[median_pos_X];
    x = (xll + xur)/2;
  } else {
    x = Xs[median_pos_X];
  }
  if (Ys.size() % 2 == 0) {
    int yll = Ys[median_pos_Y - 1];
    int yur = Ys[median_pos_Y];
    y = (yll + yur)/2;
  } else {
    y = Ys[median_pos_Y];
  }
  return median (x, y);
}

std::pair<int, int>
CellMoveRouter::compute_cell_median(odb::dbInst* cell) {
  std::vector<int>  nets_Bbox_Xs, nets_Bbox_Ys;

  for(auto pin : cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL) {
      if (net->getSigType().isSupply()) {
        continue;
      }
      for(auto iterm : net->getITerms())
      {
        odb::dbInst* inst = iterm->getInst();
        int x=0, y=0;
        //Using Cell location (fast)
        
        if(inst && inst != cell)// is connected
        {
          inst->getLocation(x, y);
          nets_Bbox_Xs.push_back(x);
          nets_Bbox_Ys.push_back(y);
        }
      }
      
      for (auto bterm : net->getBTerms()) {
        int x=0, y=0;
        const bool pinExist = bterm->getFirstPinLocation(x, y);
        if(pinExist) {
          nets_Bbox_Xs.push_back(x);
          nets_Bbox_Ys.push_back(y);
        }
      }
    }
  }
  return nets_Bboxes_median(nets_Bbox_Xs, nets_Bbox_Ys);
}

std::pair<int, int>
CellMoveRouter::compute_cells_nets_median(odb::dbInst* cell) {
  std::vector<int>  nets_Bbox_Xs, nets_Bbox_Ys;

  for(auto pin : cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL) {
      if (net->getSigType().isSupply()) {
        continue;
      }

      int xll = std::numeric_limits<int>::max();
      int yll = std::numeric_limits<int>::max();
      int xur = std::numeric_limits<int>::min();
      int yur = std::numeric_limits<int>::min();
      for(auto iterm : net->getITerms())
      {
        int x, y;
        //Using Cell location (fast)
        odb::dbInst* inst = iterm->getInst();
        if(inst && inst != cell) // is connected
        {
          inst->getLocation(x, y);
          xur = std::max(xur, x);
          yur = std::max(yur, y);
          xll = std::min(xll, x);
          yll = std::min(yll, y);
        }
      }

      for (auto bterm : net->getBTerms()) {
        int x=0, y=0;
        const bool pinExist = bterm->getFirstPinLocation(x, y);

        if(pinExist) {
          //logger_->report("Net: {}", net->getName());
          xur = std::max(xur, x);
          yur = std::max(yur, y);
          xll = std::min(xll, x);
          yll = std::min(yll, y);
        }
      }

      nets_Bbox_Xs.push_back(xur);
      nets_Bbox_Xs.push_back(xll);
      nets_Bbox_Ys.push_back(yur);
      nets_Bbox_Ys.push_back(yll);
    }
  }

  return nets_Bboxes_median(nets_Bbox_Xs, nets_Bbox_Ys);
}

std::pair<int, int>
CellMoveRouter::compute_net_median(odb::dbNet* net) {
  std::vector<int>  nets_Bbox_Xs, nets_Bbox_Ys;

  for(auto pin : net->getITerms())
  {
    int x=0, y=0;
    //Using Cell location (fast)
    odb::dbInst* inst = pin->getInst();
    if(inst)// is connected
    {
      inst->getLocation(x, y);
      nets_Bbox_Xs.push_back(x);
      nets_Bbox_Ys.push_back(y);
    }
  }

  for (auto bterm : net->getBTerms()) {
    int x=0, y=0;
    const bool pinExist = bterm->getFirstPinLocation(x, y);
    if(pinExist) {
      nets_Bbox_Xs.push_back(x);
      nets_Bbox_Ys.push_back(y);
    }
  }

  return nets_Bboxes_median(nets_Bbox_Xs, nets_Bbox_Ys);
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

void
CellMoveRouter::InitCellsWeight()
{
  odb::dbBlock *block = db_->getChip()->getBlock(); //pega o bloco
  if(stt_ == nullptr) {
    stt_ = ord::OpenRoad::openRoad()->getSteinerTreeBuilder(); // create object before using
    block->setDrivingItermsforNets(); //set net drivers
  }
  cells_weight_.clear();
  cells_to_move_.clear();
  std::map <std::string, int> netSteinerLookup; //mapa de nets e stwl

  for (auto net: block->getNets()){ //cálculo do delta hpwl-wl de uma net
    if (net->getSigType().isSupply()) {
      net->setSpecial();
      continue;
    }
    
    auto netName = net->getName(); //pega o nome desta net

    int estimate_wl=0;
  
    auto tree = buildSteinerTree(net); //make net steiner tree
    estimate_wl = getTreeWl(tree); //get net STWL from tree
    netSteinerLookup[netName] = estimate_wl;
  }

  for(auto cell : block->getInsts()) {
    int delta_sum = 0, steiner_sum = 0;
    if(cell->isFixed()) {
      continue;
    }
    if(cell->isBlock()) {
      std::cout<<"É um Bloco"<<std::endl;
      continue;
    }

    if(isClockBuffer(cell) || isSequential(cell)) {
      continue;
    }
    int original_x, original_y;
    cell->getLocation(original_x, original_y);
    median cellMedian = compute_cells_nets_median(cell);
    if(cellMedian.first == 0 && cellMedian.second == 0) {
      continue;
    }

    // Mudar aqui o calculo da soma
    for (auto pin : cell->getITerms()) {
      auto net = pin->getNet();
      if(net != nullptr) {
        if (net->getSigType().isSupply()) {
          continue;
        }
        int pinX, pinY;
        pin->getAvgXY(&pinX, &pinY);
        sta::Slack slack_weight = slackWeigthSimple(pin);
        int moveX = cellMedian.first - original_x;
        int moveY = cellMedian.second - original_y;
        auto tree = buildSteinerTree(net, pin, pinX + moveX, pinY + moveY); //make net steiner tree
        steiner_sum += netSteinerLookup[net->getName()];

        delta_sum += ((netSteinerLookup[net->getName()] - getTreeWl(tree)) * slack_weight);
      }
    }

    RcmCell cell_weight = {.inst=cell, .iterm=nullptr, .weight= ((double) delta_sum), .distance_to_mediana=0, .init_stwl=steiner_sum, .mediana={0,0}};
    cells_weight_.push_back(cell_weight);
  }
  std::sort(cells_weight_.begin(),cells_weight_.end(),
            [](const RcmCell a, const RcmCell b) {
                return a.weight < b.weight;
            });
}

sta::Slack CellMoveRouter::slackWeigthSimple(odb::dbITerm* term)
{
  sta::Graph* timin_graph = openSta_->graph();
  sta::Pin* pin = network_->dbToSta(term);
  sta::Vertex * vertex = timin_graph->pinLoadVertex(pin);
  sta::Slack setup_slack = openSta_->slack(vertex, sta::MinMax::max());

  if(setup_slack >= 0.0) {
    return 0.0;
  }

  return 1.0;
}

sta::Slack CellMoveRouter::slackWeigthExp(odb::dbITerm* term)
{
  sta::Graph* timin_graph = openSta_->graph();
  sta::Pin* pin = network_->dbToSta(term);
  sta::Vertex * vertex = timin_graph->pinLoadVertex(pin);
  sta::Slack setup_slack = openSta_->slack(vertex, sta::MinMax::max());

  if(setup_slack >= 0.0) {
    return 0.0;
  }

  auto normalized_slack = std::abs(setup_slack / worst_slack_);
  return std::exp(2*normalized_slack);
}

sta::Slack CellMoveRouter::slackWeigthExpSum(odb::dbITerm* term)
{
  sta::Graph* timin_graph = openSta_->graph();
  sta::Pin* pin = network_->dbToSta(term);
  sta::Vertex * vertex = timin_graph->pinLoadVertex(pin);
  sta::Slack setup_slack = openSta_->slack(vertex, sta::MinMax::max());

  if(setup_slack >= 0.0) {
    return 0.0;
  }

  return 1.0;
}

void
CellMoveRouter::sortCellsToMoveMedian() {
  for (auto cell : cells_to_move_) {
    int cell_x, cell_y;
    cell.inst->getLocation(cell_x, cell_y);
    median mediana = compute_cells_nets_median(cell.inst);
    int dist_to_mediana = compute_manhattan_distance(mediana, {cell_x, cell_y});
    cell.mediana = mediana;
    cell.distance_to_mediana = dist_to_mediana;
  }
  std::sort(cells_to_move_.begin(),cells_to_move_.end(),
          [](const RcmCell a, const RcmCell b) {
              return a.distance_to_mediana > b.distance_to_mediana;
          });
}

int
CellMoveRouter::compute_manhattan_distance(median loc1, median loc2) {
  int distance = std::abs(loc1.first - loc2.first) + std::abs(loc1.second - loc2.second);
  return distance;
}

void
CellMoveRouter::testRevertingRouting()
{
  auto block = db_->getChip()->getBlock();
  icr_grt_ = new grt::IncrementalGRoute(grt_, block);

  // Inital Global Rout by OpenROAD
  grt_->globalRoute();

  long init_wl = grt_->computeWirelength();
  std::cout<<"initial wl  "<<init_wl<<std::endl;
  odb::dbInst* moving_cell;
  for(auto cell: block->getInsts()) {
    if(cell->getName() == "inst111221") {
      moving_cell = cell;
      break;
    }
  }
  std::cout<<"cell name: "<<moving_cell->getName()<<std::endl;
  std::map<odb::dbNet*, grt::GRoute>  affected_nets;

  for(auto pin : moving_cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL){
      if (net->getSigType().isSupply()) {
        continue;
      }
      grt::GRoute net_init_route = grt_->getNetRoute(net);
      affected_nets[net] =  net_init_route;
      /*logger_->report("Net {}:", net->getName());
      for(auto segment : net_init_route) {
        logger_->report(" ({}, {}, {}) -> ({}, {}, {})",
                        segment.init_x,
                        segment.init_y,
                        segment.init_layer,
                        segment.final_x,
                        segment.final_y,
                        segment.final_layer);

      }*/
    }
  }

  std::vector<odb::dbNet*>rerouted_nets;
  grt_->clearDirtyNets();
  for (auto affected_net : affected_nets) {
    if(affected_net.first->getSigType().isSupply()) {
      logger_->report("Erro nas nets afetadas");
    }
    rerouted_nets.push_back(affected_net.first);
    grt_->addDirtyNet(affected_net.first);
  }
  int original_x, original_y;
  moving_cell->getLocation(original_x, original_y);

  median med = compute_cell_median(moving_cell);
  moving_cell->setLocation(med.first , med.second);
  std::cout<<"cell init pos: "<<original_x<<", "<< original_y<<std::endl;
  std::cout<<"cell final pos: "<<med.first<<", "<< med.second<<std::endl;
  std::cout<<"Nets a rerotear: "<<affected_nets.size()<<std::endl;
  std::cout<<"Usos inicio:"<<std::endl;
  grt_->reportTotalUsages();
  icr_grt_->updateRoutes();
  if(!grt_->getDirtyNets().empty()) {
    grt_->clearDirtyNets();
  }

  std::cout<<"Usos depois:"<<std::endl;
  grt_->reportTotalUsages();

  moving_cell->setLocation(original_x, original_y);
  auto nets_reroteadas = grt_->updateNetsIncr(rerouted_nets);
  std::cout<<"Usos roteamentos apagados:"<<std::endl;
  grt_->reportTotalUsages();
  std::cout<<"tamanho afected: "<<affected_nets.size()<<std::endl;
  std::cout<<"tamanho rerouted: "<<rerouted_nets.size()<<std::endl;
  // Atualizar as informações das nets com o incremental.
  
  for (auto affected_net : nets_reroteadas) {
    auto original_route = affected_nets[affected_net];
    grt_->loadGuidesFromUser(affected_net, original_route);
    logger_->report("Net {}:", affected_net->getName());
    /*for(auto segment : affected_net.second) {
      logger_->report(" ({}, {}, {}) -> ({}, {}, {})",
                      segment.init_x,
                      segment.init_y,
                      segment.init_layer,
                      segment.final_x,
                      segment.final_y,
                      segment.final_layer);

    }*/
  }
  std::cout<<"Usos como era pra ser no inicio:"<<std::endl;
  grt_->reportTotalUsages();
  delete icr_grt_;
  icr_grt_ = nullptr;
}

void
CellMoveRouter::report_nets_pins()
{
  
  auto block = db_->getChip()->getBlock();
  for (auto net: block->getNets()){ //cálculo do delta hpwl-wl de uma net
    /*if(net->getBTermCount() > 0) {
      logger_->report("Net {}, tem bterm", net->getName());
    }*/
    if(net->getName() == "pin621") {
      logger_->report("{}  {}", net->getName(), net->getITerms().size() + net->getBTerms().size());
    }
  }
}

void CellMoveRouter::runAbacus() { 
  auto block = db_->getChip()->getBlock();
  odb::Rect area = block->getCoreArea();
  abacus_.abacus(area.xMin(), area.yMin(), area.xMax(), area.yMax());
};

}
