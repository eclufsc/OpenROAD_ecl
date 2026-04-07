#include "rcm/CellMoveRouter.h"
#include "odb/db.h"
#include "gui/gui.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "grt/GlobalRouter.h"
#include "stt/SteinerTreeBuilder.h"

#include <chrono>
#include <iostream>
#include <unordered_map>
#include <vector>


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
  debug_{false}
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

void
CellMoveRouter::Cell_Move_Rerout(){

  auto grcmo_start = std::chrono::high_resolution_clock::now();
  auto block = db_->getChip()->getBlock();
  std::unordered_map<odb::dbInst*,int> cells_movement;



  // Inital Global Rout by OpenROAD
  // grt_->globalRoute();

  grt_->setCongestionReportFile("~/UFSC/mestrado/OpenROAD-flow-scripts/flow/reports/nangate45/ibex/grcmo_flow1/grcmo_congestion.rpt");
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
    int original_x, original_y;
    cell->getLocation(original_x, original_y);
    median cellMedian = compute_cells_nets_median(cell);
    if(cellMedian.first == 0 && cellMedian.second == 0) {
      continue;
    }

    for (auto pin : cell->getITerms()) {
      auto net = pin->getNet();
      if(net != nullptr) {
        if (net->getSigType().isSupply()) {
          continue;
        }
        int pinX, pinY;
        pin->getAvgXY(&pinX, &pinY);
        int moveX = cellMedian.first - original_x;
        int moveY = cellMedian.second - original_y;
        auto tree = buildSteinerTree(net, pin, pinX + moveX, pinY + moveY); //make net steiner tree
        steiner_sum += netSteinerLookup[net->getName()];
        delta_sum += netSteinerLookup[net->getName()] - getTreeWl(tree);
      }
    }

    RcmCell cell_weight = {cell, delta_sum, 0, steiner_sum, {0,0}};
    cells_weight_.push_back(cell_weight);
  }
  std::sort(cells_weight_.begin(),cells_weight_.end(),
            [](const RcmCell a, const RcmCell b) {
                return a.weight < b.weight;
            });
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
