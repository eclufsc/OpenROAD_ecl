#include "rcm/CellMoveRouter.h"
#include "odb/db.h"
#include "gui/gui.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "grt/GlobalRouter.h"

#include <vector>
#include <iostream>
#include <unordered_map>

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
  for(int i; i < rectangles_.size();i++)
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
  grt_{ord::OpenRoad::openRoad()->getGlobalRouter()},
  logger_{ord::OpenRoad::openRoad()->getLogger()},
  debug_{false}
{
}

void
CellMoveRouter::helloWorld()
{
  logger_->report("Hello World!");
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

void
CellMoveRouter::ShowFirstNetRout() {
  auto block = db_->getChip()->getBlock();
  auto nets = block->getNets();
  //creates a list of all nets
  std::vector<odb::dbNet*>  net_list;
  net_list.reserve(nets.size());
  for(auto cell : nets)
    net_list.push_back(cell); //TODO should check if a cell is a macro

  //makes the global routing
  grt_->globalRoute();

  //gets the segment for the first net
  grt::NetRouteMap routs = grt_->getRoutes();
  odb::dbNet* net1 = net_list[100];
  grt::GRoute route = routs[net1];

  for (auto segment : route)
  {
    logger_->report("{:6d} {:6d} {:2d} -> {:6d} {:6d} {:2d}",
                    segment.init_x,
                    segment.init_y,
                    segment.init_layer,
                    segment.final_x,
                    segment.final_y,
                    segment.final_layer);
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
      GCellElement el = std::pair(gcell_box, Bbox);
      //rectangleRender_->addRectangle(Bbox);
      gcellTree_->insert(el);
      prev_x = *x_it;
    }
    prev_x = *gridX.begin();
    prev_y = *y_it;
  }
}

void
CellMoveRouter::Cell_Move_Rerout(){

  auto block = db_->getChip()->getBlock();
  auto cells = block->getInsts();
  std::unordered_map<odb::dbInst*,int> cells_movement;

  icr_grt_ = new grt::IncrementalGRoute(grt_, block);


  // Inital Global Rout by OpenROAD
  grt_->globalRoute();

  long init_wl = grt_->computeWirelength();
  std::cout<<"initial wl  "<<init_wl<<std::endl;
  gui::Gui* gui = gui::Gui::get();
  if (rectangleRender_ == nullptr)
  {
    rectangleRender_ = std::make_unique<RectangleRender>();
    gui->registerRenderer(rectangleRender_.get());
  }

  InitCellsWeight();
  //Initalize Rtrees
  InitCellTree();
  InitGCellTree();
  abacus_.InitRowTree();

  int total_moved = 0;
  int total_regected = 0;
  int iterations = 0;
  int n_move_cells = std::floor(cells_weight_.size() * 5/100);
  while(total_moved < n_move_cells){
    InitCellsWeight();
    int n_cells = cells_weight_.size();
    std::cout<<"Celulas a serem movidas  "<<n_move_cells<<std::endl;
    for(int i = cells_weight_.size() - 1; i >=0; i--) {
      if(i < n_cells - n_move_cells) {
        break;
      }
      cells_to_move_.push_back(cells_weight_[i].second);
    }

    int cont = 0;
    int cont2 = 0;
    int failed = 0;
    int worse = 0;
    while(!cells_to_move_.empty()) {
      auto moving_cell = cells_to_move_[0];
      //std::cout<<"iter: "<<cont+1<<"\n   cell"<<moving_cell->getName()<<std::endl;
      bool complete = Swap_and_Rerout(moving_cell, failed, worse);
      if(complete) {
        cells_movement[moving_cell] += 1;
        cont++;
      } else {
        cont2++;
        cells_to_move_.erase(cells_to_move_.begin());
      }
    }
    total_moved += cont;
    total_regected += cont2;
    iterations ++;
    std::cout<<"Celulas movidas: "<<cont<<std::endl;
    std::cout<<"Celulas rejeitadas: "<<cont2<<std::endl;
    std::cout<<"Celulas movidas total: "<<total_moved<<std::endl;
    std::cout<<"move worsed cells  "<<worse<<std::endl;
  }

  std::cout<<"moved cells  "<<total_moved<<std::endl;
  std::cout<<"rejected cells  "<<total_regected<<std::endl;
  long after_wl = grt_->computeWirelength();
  std::cout<<"final wl  "<<after_wl<<std::endl;
  std::cout<<"iterations  "<<iterations<<std::endl;

  for(auto [inst, count] : cells_movement) {
    std::cout<<"Inst "<<inst->getName()<<" moved: "<<count<<" times"<< std::endl;
  }
  std::cout<<"Effectvly moved: "<< cells_movement.size()<<std::endl;

  delete icr_grt_;
  icr_grt_ = nullptr;

}

bool
CellMoveRouter::Swap_and_Rerout(odb::dbInst * moving_cell,
                                int& failed_legalization,
                                int& worse_wl) {
  auto block = db_->getChip()->getBlock();
  std::vector<std::pair<odb::dbNet*, grt::GRoute>>  affected_nets;
  std::vector<int>  nets_Bbox_Xs;
  std::vector<int>  nets_Bbox_Ys;
  int moving_cell_width = moving_cell->getBBox()->getDX();
  gui::Gui* gui = gui::Gui::get();
  //Finding the cell's nets bounding boxes
  int original_x, original_y;
  moving_cell->getLocation(original_x, original_y);
  int before_hwpl = 0;

  for(auto pin : moving_cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL){
      if (net->getSigType().isSupply()) {
        continue;
      }
      before_hwpl += getNetHPWLFast(net);

      int xll = std::numeric_limits<int>::max();
      int yll = std::numeric_limits<int>::max();
      int xur = std::numeric_limits<int>::min();
      int yur = std::numeric_limits<int>::min();
      for(auto iterm : net->getITerms())
      {
        int x=0, y=0;
        //Using Cell location (fast)
        odb::dbInst* inst = iterm->getInst();
        if(inst)// is connected
        {
          inst->getLocation(x, y);
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
      affected_nets.push_back(std::make_pair(net, net_init_route));
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
  xur = std::min(xur, result[0].second.xMax() + 14 * gcell_height);
  yur = std::min(yur, result[0].second.yMax());
  xll = std::max(xll, result[0].second.xMin() - 14 * gcell_height);
  yll = std::max(yll, result[0].second.yMin());
  auto [best_x, best_y, has_enoght_space] = abacus_.get_free_spaces(moving_cell_width, xll, yll, xur, yur);
  //abacus_.get_free_spaces_old(xll, yll, xur, yur);
  
  if(!has_enoght_space) {
    //logger_->report("Sem espaço!");
    return false;
  }

  moving_cell->setLocation(best_x, best_y.yMin());
  int after_hwpl = 0;
  for(auto pin : moving_cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL){
      if (net->getSigType().isSupply()) {
        continue;
      }
      after_hwpl += getNetHPWLFast(net);
    }
  }
  if(after_hwpl > before_hwpl || (best_x == original_x && best_y.yMin() == original_y)) {
    moving_cell->setLocation(original_x, original_y); 
    return false;
  }

  //Call abacus for legalization area
  rectangleRender_->addRectangle(odb::Rect(xll, best_y.yMin(), xur, best_y.yMax()));
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
      //drawRectangle(xur, yur, xll, yll);
    }
  }

  //Put back!!!!!!!!
  for(auto [cell, original_location] : changed_cells) {
    if(cell == moving_cell) {
      continue;
    }

    for(auto pin : cell->getITerms())
    {
      auto affected_net = pin->getNet();
      if(affected_net != NULL){
        if (affected_net->getSigType().isSupply()) {
          continue;
        }
        //wl_before_moving += grt_->computeNetWirelength(affected_net);
        grt::GRoute net_init_route = grt_->getNetRoute(affected_net);
        affected_nets.push_back(std::make_pair(affected_net, net_init_route));
      }
    }
  }

  //std::cout<<"Reroteando nets afetadas....."<<std::endl;
  //clear dirty nets and update the new nets ot be rerouted
  std::vector<odb::dbNet*>rerouted_nets;
  grt_->clearDirtyNets();
  for (auto affected_net : affected_nets) {
    if(affected_net.first->getSigType().isSupply()) {
      logger_->report("Erro nas nets afetadas");
    }
    rerouted_nets.push_back(affected_net.first);
    grt_->addDirtyNet(affected_net.first);
  }

  icr_grt_->updateRoutes();
  if(!grt_->getDirtyNets().empty()) {
    grt_->clearDirtyNets();
  }

  gui->redraw();

  for(auto pin : moving_cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL){
      if (net->getSigType().isSupply()) {
        continue;
      }
      for (auto iterm : net->getITerms()) {
        auto inst = iterm->getInst();
        auto erase = std::find(cells_to_move_.begin(), cells_to_move_.end(), inst);
        if(erase != cells_to_move_.end()) {
          cells_to_move_.erase(erase);
        }
      }
    }
  }
  //std::cout<<"nets afetadas reroteadas..."<<std::endl;
  return true;
}

std::pair<int, int>
CellMoveRouter::nets_Bboxes_median(std::vector<int>& Xs, std::vector<int>& Ys) {

  int median_pos_X = std::floor(Xs.size()/2);
  std::sort(Xs.begin(), Xs.end());

  int median_pos_Y = std::floor(Ys.size()/2);
  std::sort(Ys.begin(), Ys.end());

  int xll = Xs[median_pos_X - 1];
  int xur = Xs[median_pos_X];
  int yll = Ys[median_pos_Y - 1];
  int yur = Ys[median_pos_Y];

  int x = (xll + xur)/2;
  int y = (yll + yur)/2;

  return median (x, y);
}

std::pair<int, int>
CellMoveRouter::compute_cell_median(odb::dbInst* cell) {
  std::vector<int>  nets_Bbox_Xs, nets_Bbox_Ys;

  for(auto pin : cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL) {
      for(auto iterm : net->getITerms())
      {
        int x=0, y=0;
        //Using Cell location (fast)
        odb::dbInst* inst = iterm->getInst();
        if(inst)// is connected
        {
          inst->getLocation(x, y);
          nets_Bbox_Xs.push_back(x);
          nets_Bbox_Ys.push_back(y);
        }
      }
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

  return nets_Bboxes_median(nets_Bbox_Xs, nets_Bbox_Ys);
}


void
CellMoveRouter::InitCellsWeight()
{
  cells_weight_.clear();
  cells_to_move_.clear();
  odb::dbBlock *block = db_->getChip()->getBlock(); //pega o bloco
  auto cellNumber = block->getInsts().size();
  
  std::map <std::string, int> netDeltaLookup; //mapa de nets e hpwl,stwl

  for (auto net: block->getNets()){ //cálculo do delta hpwl-wl de uma net
    
    auto netName = net->getName(); //pega o nome desta net

    int hpwl=0, routing_wl=0;
  
    hpwl = getNetHPWLFast(net);

    routing_wl = grt_->computeNetWirelength(net); //transformei esse metodo pra public - WL da net

    int netDelta = routing_wl - hpwl; //calculo do delta da net/pino
    netDeltaLookup[netName] = netDelta;
  }

  for(auto cell : block->getInsts()) {
    int delta_sum = 0;
    if(cell->isFixed()) {
      cells_weight_.push_back(std::make_pair(delta_sum, cell));
      continue;
    }
    if(cell->isBlock()) {
      std::cout<<"É um Bloco"<<std::endl;
      continue;
    }
    for (auto pin : cell->getITerms()) {
      auto net = pin->getNet();
      if(net != nullptr) {
        if (net->getSigType().isSupply()) {
          continue;
        }
        delta_sum += netDeltaLookup[net->getName()];
      }
    }
    cells_weight_.push_back(std::make_pair(delta_sum, cell));
  }
  std::sort(cells_weight_.begin(),cells_weight_.end());
  
}

void
CellMoveRouter::InitNetsWeight() {
  nets_weight_.clear();
  odb::dbBlock *block = db_->getChip()->getBlock(); //pega o bloco
  
  std::map <std::string, int> netDeltaLookup; //mapa de nets e hpwl,stwl

  for (auto net: block->getNets()){ //cálculo do delta hpwl-wl de uma net

    int hpwl=0, routing_wl=0;
  
    hpwl = getNetHPWLFast(net);

    routing_wl = grt_->computeNetWirelength(net); //transformei esse metodo pra public - WL da net


    int delta = routing_wl - hpwl;
    nets_weight_.push_back(std::make_pair(delta, net));
  }
  std::sort(nets_weight_.begin(),nets_weight_.end());
}

void
CellMoveRouter::SelectCellsToMove() {
  auto block = db_->getChip()->getBlock();
  auto cells = block->getInsts();
  std::unordered_map<odb::dbInst*,int> cells_movement;

  // Init incremental global router
  icr_grt_ = new grt::IncrementalGRoute(grt_, block);

  //Initalize Rtrees
  InitCellTree();
  InitGCellTree();
  abacus_.InitRowTree();

  // Inital Global Rout by OpenROAD
  grt_->globalRoute();
  long init_wl = grt_->computeWirelength();
  std::cout<<"initial wl  "<<init_wl<<std::endl;

  InitNetsWeight();

  for(int i = nets_weight_.size() - 1; i >=0; i--) {
    odb::dbNet* net = nets_weight_[i].second;
    median net_median = compute_net_median(net);

    odb::dbInst* cell_to_move = nullptr;
    int smalledt_dis = std::numeric_limits<int>::max();
    
    for(auto iterm : net->getITerms()) {
      odb::dbInst* candidate_cell = iterm->getInst();

      if(candidate_cell->isFixed() || candidate_cell->isBlock()) {
        continue;
      }

      median candidate_cell_median = compute_cell_median(candidate_cell);
      int distance = compute_manhattan_distance(net_median, candidate_cell_median);
      if(distance < smalledt_dis) {
        smalledt_dis = distance;
        cell_to_move = candidate_cell;
      }
    }
    if(cell_to_move != nullptr)
    cells_to_move_.push_back(cell_to_move);
  }
}

/*void
CellMoveRouter::moveSelectedCells() {
  int total_moved = 0;
  int total_regected = 0;
  int failed = 0;
  int worse = 0;
  std::cout<<"I"
  while(!cells_to_move_.empty()) {
    auto moving_cell = cells_to_move_[0];
    //std::cout<<"iter: "<<cont+1<<"\n   cell"<<moving_cell->getName()<<std::endl;
    bool complete = Swap_and_Rerout(moving_cell, failed, worse);
    if(complete) {
      cells_movement[moving_cell] += 1;
      total_moved++;
    } else {
      total_regected++;
      cells_to_move_.erase(cells_to_move_.begin());
    }
  }
  std::cout<<"Celulas movidas: "<<total_moved<<std::endl;
  std::cout<<"Celulas rejeitadas: "<<total_regected<<std::endl;
  std::cout<<"Celulas movidas total: "<<total_moved<<std::endl;
  std::cout<<"move worsed cells  "<<worse<<std::endl;
}*/

int
CellMoveRouter::compute_manhattan_distance(median loc1, median loc2) {
  int distance = std::abs(loc1.first - loc2.first) + std::abs(loc1.second - loc2.second);
  return distance;
}

int
CellMoveRouter::getNetHPWLFast(odb::dbNet * net) const
{
  int xll = std::numeric_limits<int>::max();
  int yll = std::numeric_limits<int>::max();
  int xur = std::numeric_limits<int>::min();
  int yur = std::numeric_limits<int>::min();
  for(auto iterm : net->getITerms())
  {
    int x=0, y=0;
    //Using Cell LL location (fast)
    odb::dbInst* inst = iterm->getInst();
    if(inst)// is connected
    {
      inst->getLocation(x, y);
      xur = std::max(xur, x);
      yur = std::max(yur, y);
      xll = std::min(xll, x);
      yll = std::min(yll, y);
    }
  }
  const int width = std::abs(xur-xll);
  const int height = std::abs(yur-yll);
  int hpwl = width + height;
  return hpwl;
}

void
CellMoveRouter::report_nets_pins()
{
  auto block = db_->getChip()->getBlock();
  for (auto net: block->getNets()){ //cálculo do delta hpwl-wl de uma net
    logger_->report("{}  {}", net->getName(), net->getITerms().size() + net->getBTerms().size());
  }
}

}
