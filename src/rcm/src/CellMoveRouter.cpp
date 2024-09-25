#include "rcm/CellMoveRouter.h"
#include "odb/db.h"
#include "gui/gui.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "grt/GlobalRouter.h"
#include "stt/SteinerTreeBuilder.h"

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

stt::Tree
CellMoveRouter::buildSteinerTree(odb::dbNet * net)
{

  if ((net->getSigType() == odb::dbSigType::GROUND)
      || (net->getSigType() == odb::dbSigType::POWER)
      || (net->getITermCount() + net->getBTermCount() < 2))
    return stt::Tree{};

  const int driverID = net->getDrivingITerm();
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
      if(driverID == dbITerm->getId())
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

int
CellMoveRouter::getTreeWl(const stt::Tree &tree) //get steiner wirelength from steiner tree object
{
  int treeWl = 0;
  
  for(int i = 0; i < tree.branchCount(); ++i)
  {
    const stt::Branch& branch = tree.branch[i];
    if(i == branch.n)
      continue;
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
  std::cout<<"initial #vias  "<<grt_->getViaCount()<<std::endl;
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
  int total_worse = 0;
  int iterations = 0;
  int n_move_cells = 0;
  for(int j = 0; j < 20; j++) {
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
    int cont = 0;
    int cont2 = 0;
    int failed = 0;
    int worse = 0;
    while(!cells_to_move_.empty()) {
      auto moving_cell = cells_to_move_[0].inst;
      bool complete = Swap_and_Rerout(moving_cell, failed, worse, cells_to_move_[0].init_stwl);
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
    total_worse += worse;
    iterations ++;
    long after_wl = grt_->computeWirelength();
    std::cout<<"iteração: "<<iterations<<std::endl;
    std::cout<<"wl (um): "<<after_wl<<std::endl;
    std::cout<<"#vias: "<<grt_->getViaCount()<<std::endl;
    std::cout<<"movimentos: "<<cont<<std::endl;
    std::cout<<"movidas: "<< cells_movement.size()<<std::endl;
    std::cout<<"rejeitadas: "<<cont2<<std::endl;
    std::cout<<"worse: "<<worse<<std::endl;

  }

  std::cout<<"\ntotal moved cells  "<<total_moved<<std::endl;
  std::cout<<"total rejected cells  "<<total_regected<<std::endl;
  std::cout<<"total move worsed cells  "<<total_worse<<std::endl;
  long after_wl = grt_->computeWirelength();
  std::cout<<"final wl  "<<after_wl<<std::endl;
  std::cout<<"final #vias  "<<grt_->getViaCount()<<std::endl;
  std::cout<<"iterations  "<<iterations<<std::endl;

  /*for(auto [inst, count] : cells_movement) {
    std::cout<<"Inst "<<inst->getName()<<" moved: "<<count<<" times"<< std::endl;
  }*/
  std::cout<<"Effectvly moved: "<< cells_movement.size()<<std::endl;

  delete icr_grt_;
  icr_grt_ = nullptr;

}

bool
CellMoveRouter::Swap_and_Rerout(odb::dbInst * moving_cell,
                                int& failed_legalization,
                                int& worse_wl,
                                int before_estimate) {
  auto block = db_->getChip()->getBlock();
  std::map<odb::dbNet*, grt::GRoute>  affected_nets;
  std::vector<int>  nets_Bbox_Xs;
  std::vector<int>  nets_Bbox_Ys;
  int moving_cell_width = moving_cell->getBBox()->getDX();
  gui::Gui* gui = gui::Gui::get();
  //Finding the cell's nets bounding boxes
  int original_x, original_y;
  //logger_->report("moving cell: {}", moving_cell->getName()); 
  moving_cell->getLocation(original_x, original_y);
  if(!use_steiner_) {
    before_estimate = 0;
  }

  for(auto pin : moving_cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL){
      if (net->getSigType().isSupply()) {
        continue;
      }
      if(!use_steiner_) {
        before_estimate += getNetHPWLFast(net);
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
        
        rectangleRender_->addRectangle(bterm->getBBox());
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

  if(best_x == original_x && best_y.yMin() == original_y) {
    return false;
  }

  moving_cell->setLocation(best_x, best_y.yMin());
  int after_estimate = 0;
  for(auto pin : moving_cell->getITerms())
  {
    auto net = pin->getNet();
    if(net != NULL){
      if (net->getSigType().isSupply()) {
        continue;
      }
      if(use_steiner_) {
        auto tree = buildSteinerTree(net); //make net steiner tree
        after_estimate += getTreeWl(tree);
      } else {
        after_estimate += getNetHPWLFast(net);
      }
    }
  }
  if(after_estimate > before_estimate) {
    moving_cell->setLocation(original_x, original_y); 
    return false;
  }

  //Call abacus for legalization area
  //rectangleRender_->addRectangle(odb::Rect(xll, best_y.yMin(), xur, best_y.yMax()));
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
        affected_nets[affected_net] = net_init_route;
      }
    }
  }

  //std::cout<<"Reroteando nets afetadas....."<<std::endl;
  //clear dirty nets and update the new nets ot be rerouted
  std::vector<odb::dbNet*>rerouted_nets;
  grt_->clearDirtyNets();
  for (auto [affected_net, net_route] : affected_nets) {
    if(affected_net->getSigType().isSupply()) {
      logger_->report("Erro nas nets afetadas");
    }
    rerouted_nets.push_back(affected_net);
    grt_->addDirtyNet(affected_net);
  }

  icr_grt_->updateRoutes();
  if(!grt_->getDirtyNets().empty()) {
    grt_->clearDirtyNets();
  }

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
    /*logger_->report("cell: {}", moving_cell->getName());
    logger_->report("original pos: ({}, {})", original_x, original_y);
    logger_->report("current pos: ({}, {})", curr_x, curr_y);

    moving_cell->setLocation(original_x, original_y);
    for (auto [inst, original_location] : changed_cells) {
      if(inst == moving_cell) {
        continue;
      }
      /*int x_atual, y_atual;
      inst->getLocation(x_atual, y_atual);
      if(x_atual == original_location.first && y_atual == original_location.second) {
        logger_->report("Inst não moveu: {}", inst->getName());
      }
      inst->setLocation(original_location.first, original_location.second);
    }
    /*std::cout<<"\nUsos iniciais:"<<std::endl;
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
    /*std::cout<<"\nUsos depois do updateNets:"<<std::endl;
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
    /*std::cout<<"Usos dpeois:"<<std::endl;
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

  int median_pos_Y = std::floor(Ys.size()/2);
  std::sort(Ys.begin(), Ys.end());

  if(Xs.size() == 1) {
    return median(Xs[median_pos_X], Ys[median_pos_Y]);
  }


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
        
        rectangleRender_->addRectangle(bterm->getBBox());
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


void
CellMoveRouter::InitCellsWeight()
{
  cells_weight_.clear();
  cells_to_move_.clear();

  odb::dbBlock *block = db_->getChip()->getBlock(); //pega o bloco
  auto cellNumber = block->getInsts().size();

  if(stt_ == nullptr) {
    stt_ = ord::OpenRoad::openRoad()->getSteinerTreeBuilder(); // create object before using
    block->setDrivingItermsforNets(); //set net drivers
  }
  
  std::map <std::string, int> netDeltaLookup; //mapa de nets e delta
  std::map <std::string, int> netSteinerLookup; //mapa de nets e stwl
  std::map <std::string, int> netWlLookup; //mapa de nets e WL

  for (auto net: block->getNets()){ //cálculo do delta hpwl-wl de uma net
    
    auto netName = net->getName(); //pega o nome desta net

    int estimate_wl=0, routing_wl=0;
  
    
    if(use_steiner_) {
      auto tree = buildSteinerTree(net); //make net steiner tree
      estimate_wl = getTreeWl(tree); //get net STWL from tree
      netSteinerLookup[netName] = estimate_wl;
    } else {
      estimate_wl = getNetHPWLFast(net); //transformei esse metodo pra public - WL da net
    }

    routing_wl = grt_->computeNetWirelength(net);
    netWlLookup[netName] = routing_wl;


    int netDelta = routing_wl - estimate_wl; //calculo do delta da net/pino
    netDeltaLookup[netName] = netDelta;
  }


  std::vector<std::pair<int, odb::dbInst*>> cells_weight_steiner;
  for(auto cell : block->getInsts()) {
    int delta_sum = 0, steiner_sum = 0, steiner_moved_sum = 0;
    if(cell->isFixed()) {
      cells_weight_.push_back({cell, 0, 0, 0,{0,0}});
      continue;
    }
    if(cell->isBlock()) {
      std::cout<<"É um Bloco"<<std::endl;
      continue;
    }
    int original_x, original_y;
    if(compare_stiener_) {
      
      cell->getLocation(original_x, original_y);
      
      median cell_nets_median = compute_cells_nets_median(cell);
      cell->setLocation(cell_nets_median.first, cell_nets_median.second);
      if(cell_nets_median.first == 0 && cell_nets_median.second == 0) {
        cells_weight_.push_back({cell, 0, 0, 0,{0,0}});
        continue;
      }
    }

    for (auto pin : cell->getITerms()) {
      auto net = pin->getNet();
      if(net != nullptr) {
        if (net->getSigType().isSupply()) {
          continue;
        }
        if(compare_stiener_) {
          auto tree = buildSteinerTree(net); //make net steiner tree
          steiner_moved_sum += getTreeWl(tree); //get net STWL from tree
          steiner_sum += netSteinerLookup[net->getName()];
          delta_sum += netSteinerLookup[net->getName()] - getTreeWl(tree);
        } else {
          delta_sum += netDeltaLookup[net->getName()];
        }
      }
    }
    if(compare_stiener_) {
      cell->setLocation(original_x, original_y);
    }
    RcmCell cell_weight = {cell, delta_sum, 0, steiner_sum, {0,0}};
    cells_weight_.push_back(cell_weight);
    cells_weight_steiner.push_back({steiner_sum - steiner_moved_sum, cell});
  }
  std::sort(cells_weight_.begin(),cells_weight_.end(),
            [](const RcmCell a, const RcmCell b) {
                return a.weight < b.weight;
            });
  std::sort(cells_weight_steiner.begin(),cells_weight_steiner.end(),
            [](const std::pair<int, odb::dbInst*> a, const std::pair<int, odb::dbInst*> b) {
                return a.first > b.first;
            });
}

void
CellMoveRouter::InitNetsWeight() {
  nets_weight_.clear();
  odb::dbBlock *block = db_->getChip()->getBlock(); //pega o bloco
  
  std::map <std::string, int> netDeltaLookup; //mapa de nets e hpwl,stwl

  for (auto net: block->getNets()){ //cálculo do delta hpwl-wl de uma net
    if (net == nullptr || net->getSigType().isSupply()) {
      continue;
    }

    int estimate_wl=0, routing_wl=0;


    if(use_steiner_) {
      auto tree = buildSteinerTree(net); //make net steiner tree
      estimate_wl = getTreeWl(tree); //get net STWL from tree
    } else {
      estimate_wl = getNetHPWLFast(net); //transformei esse metodo pra public - WL da net
    }

    if(compare_stiener_) {
      /*Compute position for every cell*/
      /*Get steiner for new cell pos*/
      /*Compare both stieners*/
    } else {
      routing_wl = grt_->computeNetWirelength(net);
    }

    int delta = (routing_wl - estimate_wl);// / net->getITermCount();
    nets_weight_.push_back(std::make_pair(delta, net));
  }
  std::sort(nets_weight_.begin(),nets_weight_.end());
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

void
CellMoveRouter::SelectCellsToMove() {
  auto block = db_->getChip()->getBlock();
  auto cells = block->getInsts();

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

  //Initalize Rtrees
  InitCellTree();
  InitGCellTree();
  abacus_.InitRowTree();

  InitNetsWeight();
  int prev_w = 1000000;
  int n_cells_to_move = block->getInsts().size() * 5/100;
  int n_cells_selected = 0;
  for(int i = nets_weight_.size() - 1; i >=0; i--) {
    if(prev_w < nets_weight_[i].first) {
      std::cout<<"erro no peso"<<std::endl;
      std::cout<<"Peso da atual: "<<nets_weight_[i].first<<std::endl;
      if(i != nets_weight_.size() - 1)
        std::cout<<"Peso da anterior: "<<nets_weight_[i+1].first<<std::endl;
    }
    prev_w = nets_weight_[i].first;
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
    RcmCell cell_weight = {cell_to_move, nets_weight_[i].first, 0, 0,net_median};
    if(cell_to_move != nullptr) {
      cells_to_move_.push_back(cell_weight);
      n_cells_selected += 1;
    }
    if(n_cells_selected == n_cells_to_move){
      break;
    }
  }
  std::cout<<"Ncell to move = "<<n_cells_to_move<<std::endl;
  std::cout<<"Size of selected cells: "<<cells_to_move_.size()<<std::endl;
}

void
CellMoveRouter::SelectCandidateCells() {
  auto block = db_->getChip()->getBlock();
  auto cells = block->getInsts();
  cells_to_move_.clear();
  cells_weight_.clear();

  std::map <std::string, int> netSteinerLookup; //mapa de nets e stwl
  int n_cells_to_move = block->getInsts().size() * 5/100;
  int n_cells_selected = 0;
  std::unordered_map<odb::dbNet*, odb::dbInst*> net_selected_cell;
  for(odb::dbNet* net : block->getNets()) {
    if(net->getSigType().isSupply()) {
      continue;
    }
    auto tree = buildSteinerTree(net); //make net steiner tree
    int estimate_wl = getTreeWl(tree); //get net STWL from tree
    if(estimate_wl != tree.length) {
      logger_->report("Algo errado no calculo do stwl para net {}", net->getName());
    }
    netSteinerLookup[net->getName()] = estimate_wl;

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
    net_selected_cell[net] = cell_to_move;
  }

  for(auto [net, cell_to_move] : net_selected_cell) {
    if(cell_to_move == nullptr) {
      continue;
    }
    // Move cell to median and compare the before and after steiners
    int original_x, original_y;
    cell_to_move->getLocation(original_x, original_y);
    median net_median = compute_net_median(net);
    cell_to_move->setLocation(net_median.first, net_median.second);

    int moved_estimated_wl  = 0;
    int original_estimed_wl = 0;
    int n_pins = 0;
    for(odb::dbITerm* pin : cell_to_move->getITerms()) {
      odb::dbNet* affected_net = pin->getNet();
      if (affected_net == nullptr || affected_net->getSigType().isSupply()) {
        continue;
      }
      n_pins += 1;
      auto tree = buildSteinerTree(affected_net); //make net steiner tree
      moved_estimated_wl += getTreeWl(tree); //get net STWL from tree
      original_estimed_wl += netSteinerLookup[affected_net->getName()];
    }
    cell_to_move->setLocation(original_x, original_y);
    int weight = original_estimed_wl - moved_estimated_wl;
    auto erase = findInstIteratorWeight(cell_to_move);
    if(erase != cells_weight_.end()) {
      if(weight > erase->weight) {
        erase->mediana = net_median;
        erase->init_stwl = original_estimed_wl;
        erase->weight = weight;
      }
    } else {
      RcmCell cell_weight = {cell_to_move, weight, 0, original_estimed_wl, net_median};
      cells_weight_.push_back(cell_weight);
    }
  }
  std::sort(cells_weight_.begin(),cells_weight_.end(),
            [](const RcmCell a, const RcmCell b) {
                return a.weight > b.weight;
            });
  for(int i = 0; i < cells_weight_.size(); i++) {
    if(cells_weight_[i].weight <= 0 ){
      break;
    }
    cells_to_move_.push_back(cells_weight_[i]);
  }
  logger_->report("Cells to move: {}", cells_to_move_.size());
}

void
CellMoveRouter::RunCMRO() {
  int total_moved = 0;
  int total_regected = 0;
  int failed = 0;
  int worse = 0;
  int movements = 0;
  int rejected = 0;
  auto block = db_->getChip()->getBlock();
  long after_wl;
  std::unordered_map<odb::dbInst*,int> cells_movement;
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

  //Initalize Rtrees
  InitCellTree();
  InitGCellTree();
  abacus_.InitRowTree();

  if(stt_ == nullptr) {
    stt_ = ord::OpenRoad::openRoad()->getSteinerTreeBuilder(); // create object before using
    block->setDrivingItermsforNets(); //set net drivers
  }

  // Init incremental global router
  icr_grt_ = new grt::IncrementalGRoute(grt_, block);
  for(int iteration = 1; iteration <=20; iteration++){
    movements = 0;
    rejected = 0;
    SelectCandidateCells();
    gui->redraw();
    while(!cells_to_move_.empty()) {
      auto moving_cell = cells_to_move_[0];
      //std::cout<<"iter: "<<cont+1<<"\n   cell"<<moving_cell->getName()<<std::endl;
      bool complete = MoveCell(moving_cell, worse);
      if(complete) {
        cells_movement[moving_cell.inst] += 1;
        movements++;
      } else {
        rejected++;
        cells_to_move_.erase(cells_to_move_.begin());
      }
    }
    after_wl = grt_->computeWirelength();
    std::cout<<"iteração: "<<iteration<<std::endl;
    std::cout<<"wl (um): "<<after_wl<<std::endl;
    std::cout<<"#vias: "<<grt_->getViaCount()<<std::endl;
    std::cout<<"movimentos: "<<movements<<std::endl;
    std::cout<<"movidas: "<< cells_movement.size()<<std::endl;
    std::cout<<"rejeitadas: "<<rejected<<std::endl;
    std::cout<<"worse: "<<worse<<std::endl;
    total_moved += movements;
    total_regected += rejected;
  }
  std::cout<<"Celulas movidas "<<total_moved<<std::endl;
  std::cout<<"Celulas rejeitadas "<<total_regected<<std::endl;
  std::cout<<"Celulas movidas total "<<cells_movement.size()<<std::endl;
  std::cout<<"move worsed cells  "<<worse<<std::endl;
  std::cout<<"final wl  "<<after_wl<<std::endl;
}

bool CellMoveRouter::MoveCell(RcmCell cell, int& worse_wl) {
  std::vector<std::pair<odb::dbNet*, grt::GRoute>>  affected_nets;
  std::vector<int>  nets_Bbox_Xs;
  std::vector<int>  nets_Bbox_Ys;
  gui::Gui* gui = gui::Gui::get();
  //Initial info of the cell
  auto block = db_->getChip()->getBlock();
  //int wl_before_moving = grt_->computeWirelength();
  int moving_cell_width = cell.inst->getBBox()->getDX();
  int original_x, original_y;
  cell.inst->getLocation(original_x, original_y);
  //move cell to median point
  for (auto iterm : cell.inst->getITerms()) {
    auto affected_net = iterm->getNet();
    if(affected_net == nullptr || affected_net->getSigType().isSupply()) {
      continue;
    }
    grt::GRoute net_init_route = grt_->getNetRoute(affected_net);
    affected_nets.push_back(std::make_pair(affected_net, net_init_route));
  }
  int xll = ggrid_min_x_;
  int yll = ggrid_min_y_;
  int xur = ggrid_max_x_;
  int yur = ggrid_max_y_;
  if(debug()) {
    std::cout<<"  New Position: ("<<cell.mediana.first<<", "<<cell.mediana.second<<")"<<std::endl;
  }

  //Find median Gcell
  std::vector<GCellElement> result;
  gcellTree_->query(bgi::intersects(point_t(cell.mediana.first, cell.mediana.second)), std::back_inserter(result));

  if(debug()) {
    std::cout<<"Grid limit xmax: "<<xur<<std::endl;
    std::cout<<"Grid limit xmin: "<<xll<<std::endl;
    std::cout<<"Grid limit ymax: "<<yur<<std::endl;
    std::cout<<"Grid limit ymin: "<<yll<<std::endl;
    std::cout<<"tamanho da busca: "<<result.size()<<std::endl;
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
  
  if(!has_enoght_space) {
    //logger_->report("Sem espaço!");
    return false;
  }

  cell.inst->setLocation(best_x, best_y.yMin());

  int after_estimate = 0;
  int n_pin = 0;
  for(auto pin : cell.inst->getITerms())
  {
    auto net = pin->getNet();
    if(net != nullptr){
      if (net->getSigType().isSupply()) {
        continue;
      }
      n_pin+= 1;
      auto tree = buildSteinerTree(net); //make net steiner tree
      after_estimate += getTreeWl(tree);
    }
  }
  if(after_estimate > cell.init_stwl) {
    cell.inst->setLocation(original_x, original_y); 
    return false;
  }
  auto changed_cells = abacus_.abacus(xll, best_y.yMin(), xur, best_y.yMax());

  if(abacus_.failed()) {
    //failed_legalization++;
    if(debug()) {
      rectangleRender_->addRectangle(odb::Rect(xll, best_y.yMin(), xur, best_y.yMax()));
      std::cout<<"Legalization area: ("<<xll<<", "<<best_y.yMin()<<")"<<"  ("<<xur<<", "<<best_y.yMax()<<")"<<std::endl;
      std::cout<<"Legalization area: ("<<xll<<", "<<yll<<")"<<"  ("<<xur<<", "<<yur<<")"<<std::endl;
      //drawRectangle(xur, yur, xll, yll);
    }
  }

  for(auto [ints, original_location] : changed_cells) {
    if(ints == cell.inst) {
      continue;
    }

    for(auto pin : ints->getITerms())
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

  for(auto pin : cell.inst->getITerms())
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
    worse_wl += 1;
  }  cell.inst->setLocation(original_x, original_y);
    for (auto [inst, original_location] : changed_cells) {
      inst->setLocation(original_location.first, original_location.second);
    }
    grt_->updateNetsIncr(rerouted_nets);
    // Atualizar as informações das nets com o incremental.
    for (auto affected_net : affected_nets) {
      grt_->loadGuidesFromUser(affected_net.first, affected_net.second);
    }
   std::cout<<"Tamanho das outras celulas movidas: "<<changed_cells.size()<<std::endl;
    worse_wl += 1;
    return false;
  }*/

  gui->redraw();
  //std::cout<<"nets afetadas reroteadas..."<<std::endl;
  return true;
}

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
