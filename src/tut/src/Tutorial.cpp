#include "tut/Tutorial.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "grt/GlobalRouter.h"
#include "stt/SteinerTreeBuilder.h"


#include <iostream>

int
getTreeWl(const stt::Tree &tree) //get steiner wirelength from steiner tree object
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

namespace tut {

Tutorial::Tutorial() :
  db_{ord::OpenRoad::openRoad()->getDb()},
  logger_{ord::OpenRoad::openRoad()->getLogger()},
  //stt_{ord::OpenRoad::openRoad()->getSteinerTreeBuilder()}, //seg fault crash due to null pointer if here
  grt_{ord::OpenRoad::openRoad()->getGlobalRouter()}
{
}

void
Tutorial::printHello()
{

  stt_ = ord::OpenRoad::openRoad()->getSteinerTreeBuilder(); // create object before using

  odb::dbBlock *block = db_->getChip()->getBlock(); //get the block

  auto netNumber = block->getNets().size(); //get no. of nets in block
  auto cellNumber = block->getInsts().size(); //get no. of cells in block

  block->setDrivingItermsforNets(); //set net drivers

  logger_->report("\nNo. of nets in block: {}", netNumber); ///////////////////////////////logger
  logger_->report("\nNo. of cells in block: {}", cellNumber);

  std::map <std::string, std::pair<int,int>> netDeltaMap; //net name, net <HPWL,STWL> map

  ////temp net vectors, for net debug only
  std::vector<std::pair<int,std::string>> netHpDelta; // net HPWL sum vector <HPWL,netname>
  std::vector<std::pair<int,std::string>> netStDelta; //net STWL sum vector <STWL,netname>
  int zerowl = 0; //sum of nets with wl = 0
  int zerostwl = 0; //sum of nets with stwl = 0
  ////temp net vectors, for net debug only

  std::vector<std::pair<int,std::string>> cellHpDelta; // cell HPWL sum vector <HPWL,cellname>
  std::vector<std::pair<int,std::string>> cellStDelta; //cell STWL sum vector <STWL,cellname>
  

  for (auto net: block->getNets()){ //first part: calculate HPWL, STWL of all nets ----------------------------------

    if ((net->getSigType() == odb::dbSigType::GROUND) //ignore VDD/VSS nets
      || (net->getSigType() == odb::dbSigType::POWER))
      continue;

    auto netName = net->getName(); //get net name
    auto pinCount = net->getITermCount(); //get net pin count

    logger_->report( "\nNetName: {}      NetPinCount: {}", netName, pinCount); ///////////////////////////////logger

    int hpwl=0, wl=0, netHpwlDelta=0, netStwlDelta=0;

     wl = grt_->computeNetWirelength(net); //transformei esse metodo pra public - WL da net

     if(!wl){ //if wl = 0, report and ignore net
        zerowl++;
        logger_->report(" Ignored net due to null wirelength"); ///////////////////////////////logger
        continue;
     }

    auto tree = buildSteinerTree(net); //make net steiner tree
    int stwl = getTreeWl(tree); //get STWL from tree

    if(!stwl){ //if stwl = 0, report and ignore net
        zerostwl++;
        logger_->report(" Ignored net due to null steiner wirelength"); ///////////////////////////////logger
        continue;
     }

    netDeltaMap[netName] = std::make_pair(netHpwlDelta,netStwlDelta); //create entry in map
    
    hpwl = calc_HPWL(net); //get net HPWL
    netDeltaMap[netName].first = hpwl; // insert in pair
    
    logger_->report("               HPWL: {}", hpwl); ///////////////////////////////logger
    logger_->report("               STWL: {}", stwl); ///////////////////////////////logger
    logger_->report("               WL: {}", wl); ///////////////////////////////logger

    netHpwlDelta = wl - hpwl; //calculate HPWL delta
    netDeltaMap[netName].first = netHpwlDelta; // insert in pair

    logger_->report("        HPWL Delta: {}", netHpwlDelta); ///////////////////////////////logger
    
    netStwlDelta = wl - stwl; //calculate STWL delta
    netDeltaMap[netName].second = stwl; //insert in pair

    logger_->report("        STWL Delta: {}", netStwlDelta); ///////////////////////////////logger


    ////for net debug only
    netHpDelta.push_back(std::make_pair(netHpwlDelta, netName)); //insert HPWL sum in net HPWL vector
    netStDelta.push_back(std::make_pair(netStwlDelta, netName)); //insert STWL sum in net HPWL vector
    ////for net debug only

  } //for net - first part


  ///////////temporary, for net debug only
  
  logger_->report("\n     Total of processed nets: {}", netHpDelta.size() + zerowl);
  logger_->report("     Net HPWL delta map size: {}", netHpDelta.size()); ///////////////////////////////logger
  logger_->report("     Net STWL delta map size: {}", netStDelta.size());
  logger_->report(" Ignored nets due to null WL: {}", zerowl);
  logger_->report("Ignored nets due to null STWL: {}\n", zerostwl);

  std::sort(netHpDelta.begin(), netHpDelta.end()); //sort delta vectors
  std::sort(netStDelta.begin(), netStDelta.end());

  for(auto data: netHpDelta){ //report net HPWL deltas
    auto integer = data.first;
    auto name = data.second;
    
    logger_->report(" Net HPWL Delta: {} net name: {}", integer, name); ///////////////////////////////logger
  }

  for(auto data: netStDelta){ //report net STWL deltas
    auto integer = data.first;
    auto name = data.second;
    
    logger_->report(" Net STWL Delta: {} net name: {}", integer, name); ///////////////////////////////logger
  }
  ///////////temporary, for net debug only


  for(auto cell: block->getInsts()){ //second part: calculate HPWL, STWL sum of all cells -------------------------------

    int cellHpwlSum, cellStwlSum = 0;

    auto cellNetName = cell->getName(); //get cell name
    
    logger_->report("\n            cell name: {}", cellNetName); ///////////////////////////////logger

    for(auto pin: cell->getITerms()){ //for each pin in cell

      auto pinNet = pin->getNet(); //get net attached to pin

      if(pinNet == 0) continue; //ignore pin not attached to a net

      if ((pinNet->getSigType() == odb::dbSigType::GROUND)
        || (pinNet->getSigType() == odb::dbSigType::POWER)) //ignore pin attached to VDD/VSS
      continue;

      auto pinNetName = pinNet->getName(); //get name of net attached to pin
      
      logger_->report("               pin net name: {}", pinNetName); ///////////////////////////////logger

      cellHpwlSum = netDeltaMap[pinNetName].first ++; //sum net HPWL to cell HPWL total
      cellStwlSum = netDeltaMap[pinNetName].second ++; //sum net STWL to cell STWL total
    } //for pin

    cellHpDelta.push_back(std::make_pair(cellHpwlSum, cellNetName)); //insert HPWL sum in cell HPWL vector
    cellStDelta.push_back(std::make_pair(cellStwlSum, cellNetName)); //insert STWL sum in cell HPWL vector

    logger_->report("               HPWL delta sum: {}", cellHpwlSum); ///////////////////////////////logger
    logger_->report("               STWL delta sum: {}", cellStwlSum);

  } //for cell - second part

  
  logger_->report("\n            net map size: {}", netDeltaMap.size()); ///////////////////////////////logger
  logger_->report("   cell HPWL delta map size: {}", cellHpDelta.size());
  logger_->report("   cell STWL delta map size: {}\n", cellStDelta.size());
  
  

  std::sort(cellHpDelta.begin(), cellHpDelta.end()); //sort delta vectors
  std::sort(cellStDelta.begin(), cellStDelta.end());

  for(auto data: cellHpDelta){ //report cell HPWL deltas
    auto integer = data.first;
    auto name = data.second;
    
    logger_->report(" HPWL Delta: {} -- cell name: {}", integer, name); ///////////////////////////////logger
  }

  for(auto data: cellStDelta){ //report cell STWL deltas
    auto integer = data.first;
    auto name = data.second;
    
    logger_->report(" STWL Delta: {} -- cell name: {}", integer, name); ///////////////////////////////logger
  }

  int sameIndexSum =0;

  for(int i=0; i < cellHpDelta.size(); i++){ //report which cells have HPWL and STWL in the same positions in vectors --------------------
    
    if (cellHpDelta[i].second == cellStDelta[i].second){

      logger_->report("{} is in the same position in both cell delta vectors", cellHpDelta[i].second); ///////////////////////////////logger
      sameIndexSum++;
    }
  }
  
  logger_->report( "\n{} cells in total are in the same position", sameIndexSum); ///////////////////////////////logger
  
} //metodo

stt::Tree
Tutorial::buildSteinerTree(odb::dbNet * net)
{

  if ((net->getSigType() == odb::dbSigType::GROUND)
      || (net->getSigType() == odb::dbSigType::POWER))
    return stt::Tree{};

  const int driverID = net->getDrivingITerm();
  //std::cout<<"driver id: "<<driverID<<"\n";
  if(driverID == 0 || driverID == -1){
    std::cout<<"Net without a driver"<< std::endl; //apagar
    return stt::Tree{}; //throw std::logic_error("Error, net without a driver (should we skip it?).");
  }

  // Get pin coords and driver
  std::vector<int> xcoords, ycoords;
  int rootIndex = -1;
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
  //std::cout<<"root index: "<<rootIndex<< std::endl; //apagar
  if(rootIndex == -1){
    std::cout<<"NO ROOT INDEX ERROR"<< std::endl; //apagar
    return stt::Tree{};

  }
  // Build Steiner Tree
  const stt::Tree tree = stt_->makeSteinerTree(xcoords, ycoords, rootIndex);
  return tree;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

int
Tutorial::calc_HPWL(odb::dbNet* net)
{

  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();

  for (odb::dbITerm* iterm : net->getITerms()) {
    odb::dbPlacementStatus status = iterm->getInst()->getPlacementStatus();
    if (status != odb::dbPlacementStatus::NONE
        && status != odb::dbPlacementStatus::UNPLACED) {
      int x, y;
      iterm->getAvgXY(&x, &y);
      min_x = std::min(min_x, x);
      max_x = std::max(max_x, x);
      min_y = std::min(min_y, y);
      max_y = std::max(max_y, y);
    } else {
      logger_->error(utl::STT,
                     4,
                     "Net {} is connected to unplaced instance {}.",
                     net->getName(),
                     iterm->getInst()->getName());
    }
  }

  for (odb::dbBTerm* bterm : net->getBTerms()) {
    if (bterm->getFirstPinPlacementStatus() != odb::dbPlacementStatus::NONE
        || bterm->getFirstPinPlacementStatus()
               != odb::dbPlacementStatus::UNPLACED) {
      int x, y;
      bterm->getFirstPinLocation(x, y);
      min_x = std::min(min_x, x);
      max_x = std::max(max_x, x);
      min_y = std::min(min_y, y);
      max_y = std::max(max_y, y);
    } else {
      logger_->error(utl::STT,
                     5,
                     "Net {} is connected to unplaced pin {}.",
                     net->getName(),
                     bterm->getName());
    }
  }

  int hpwl = (max_x - min_x) + (max_y - min_y);

  return hpwl;
}

void
Tutorial::printCells()
{/*
  std::cout<<"Printing all cell names:"<<std::endl;
  auto block = db_->getChip()->getBlock();
  for(auto inst : block->getInsts())
    std::cout<<inst->getName()<<std::endl;*/
}

void
Tutorial::printNets()
{
  std::cout<<"Printing all net names:"<<std::endl;
  auto block = db_->getChip()->getBlock();
  for(auto inst : block->getNets())
    std::cout<<inst->getName()<<std::endl;
}

void
Tutorial::printPins()
{
  std::cout<<"Printing all pins names:"<<std::endl;
  auto block = db_->getChip()->getBlock();
  for(auto net : block->getNets())
  {
    std::cout<<"Net: "<<net->getName()<<std::endl;
    for(auto iterm : net->getITerms()) //instance terminals são os pinos da instancia?
    {
      auto cell = iterm->getInst();
      auto cellName = cell->getName();
      auto std_pin = iterm->getMTerm(); //o que seria master terminal?
      auto pinName = std_pin->getName();
      // std::cout<<"    PinName: "<< cellName << " : "<< pinName << std::endl;
      int x=0, y=0;
      iterm->getAvgXY(&x, &y); //isso é um vetor? 
      std::cout<<"    PinName: "<< cellName << " : "<< pinName << " Position: ( " << x << " , "<< y << " )"<< std::endl;
    }

    // calculo do hpwl
  }
}

//Challenge: Traverse all nets printing the total HPWL
void
Tutorial::printHPWLs()
{
  odb::dbBlock *block = db_->getChip()->getBlock();
  for(auto net : block->getNets())
  {
    logger_->report("Net: "+net->getName());
    int xll = std::numeric_limits<int>::max();
    int yll = std::numeric_limits<int>::max();
    int xur = std::numeric_limits<int>::min();
    int yur = std::numeric_limits<int>::min();
    for(auto iterm : net->getITerms())
    {
      int x=0, y=0;
      const bool pinExist = iterm->getAvgXY(&x, &y);
      if(pinExist)
      {
        const std::string pin_str = "Pin: "+iterm->getInst()->getName()+
                                    ":"+iterm->getMTerm()->getName()+
                                    " x:"+std::to_string(x)+" y:"+std::to_string(y);
        logger_->report(pin_str);
        xur = std::max(xur, x);
        yur = std::max(yur, y);
        xll = std::min(xll, x);
        yll = std::min(yll, y);
      }
    }
    const int width = std::abs(xur-xll);
    const int height = std::abs(yur-yll);
    const int hpwl = width + height;
    logger_->report("HPWL: "+std::to_string(hpwl));
  }
}

Tutorial::~Tutorial()
{
  //clear();
}


} //namespace
