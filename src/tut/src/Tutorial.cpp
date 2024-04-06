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
  auto cellNumber = db_->getChip()->getBlock()->getInsts().size(); //get no. of cells in block

  block->setDrivingItermsforNets(); //set net driver
  std::cout<<"\nNo. of cells in block: "<< cellNumber << std::endl; ///////////////////////////////cout

  std::map <std::string, std::pair<int,int>> deltaMap; //net name, <HPWL,STWL> map
  std::vector<std::pair<int,std::string>> hpDelta; // cell HPWL sum vector
  std::vector<std::pair<int,std::string>> stDelta; //cell STWL sum vector
  
  int i = 0; //for net number report cout

  for (auto net: block->getNets()){ //first part: calculate HPWL, STWL of all nets

    if ((net->getSigType() == odb::dbSigType::GROUND) //ignore VDD/VSS nets
      || (net->getSigType() == odb::dbSigType::POWER))
      continue;

    auto netName = net->getName(); //get net name
    auto pinCount = net->getITermCount(); //get net pin count
    std::cout<<"\nNetName: "<< netName <<"      NetPinCount: "<< pinCount << std::endl; ///////////////////////////////cout

    int hpwl=0, wl=0, netHpwlDelta=0, netStwlDelta=0;

    auto tree = buildSteinerTree(net); //make net steiner tree
    int stwl = getTreeWl(tree); //get STWL from tree

    deltaMap[netName] = std::make_pair(netHpwlDelta,netStwlDelta); //create entry in map
    
    hpwl = calc_HPWL(net); //get net HPWL
    deltaMap[netName].first = hpwl; // insert in pair
    std::cout<<"               HPWL: "<< hpwl << std::endl; ///////////////////////////////cout

    std::cout<<"               STWL: "<< stwl << std::endl; ///////////////////////////////cout

    wl = grt_->computeNetWirelength(net); //transformei esse metodo pra public - WL da net
    std::cout<<"               WL: "<< wl << std::endl; ///////////////////////////////cout

    netHpwlDelta = wl - hpwl; //calculate HPWL delta
    deltaMap[netName].first = netHpwlDelta; // insert in pair
    std::cout<<"        HPWL Delta: "<< netHpwlDelta << std::endl; ///////////////////////////////cout
    
    netStwlDelta = wl - stwl; //calculate STWL delta
    deltaMap[netName].second = stwl; //insert in pair
    std::cout<<"        STWL Delta: "<< netStwlDelta << std::endl; ///////////////////////////////cout

    i++; //for net number report cout

  } //for

  std::cout<<"\nNets: "<< i << std::endl; ///////////////////////////////cout

  for(auto cell: block->getInsts()){ //second part: calculate HPWL, STWL sum of all cells

    int cellHpwlSum, cellStwlSum = 0;

    auto cellNetName = cell->getName(); //get cell name
    std::cout<<"\n            cell name: "<< cellNetName << std::endl; ///////////////////////////////cout

    for(auto pin: cell->getITerms()){ //for each pin in cell

      auto pinNet = pin->getNet(); //get net attached to pin

      if(pinNet == 0) continue; //ignore pin not attached to a net

      if ((pinNet->getSigType() == odb::dbSigType::GROUND)
        || (pinNet->getSigType() == odb::dbSigType::POWER)) //ignore pin attached to VDD/VSS
      continue;

      auto pinNetName = pinNet->getName(); //get name of net attached to pin
      std::cout<<"               pin net name: "<< pinNetName << std::endl; ///////////////////////////////cout

      cellHpwlSum = deltaMap[pinNetName].first ++; //sum net HPWL to cell HPWL total
      cellStwlSum = deltaMap[pinNetName].second ++; //sum net STWL to cell STWL total
    } //for

    hpDelta.push_back(std::make_pair(cellHpwlSum, cellNetName)); //insert HPWL sum in cell HPWL vector
    stDelta.push_back(std::make_pair(cellStwlSum, cellNetName)); //insert STWL sum in cell HPWL vector

    std::cout<<"               HPWL delta sum: "<< cellHpwlSum << std::endl; ///////////////////////////////cout
    std::cout<<"               STWL delta sum: "<< cellStwlSum << std::endl; ///////////////////////////////cout

  } //for

  std::cout<< "\n            cell map size: " << deltaMap.size() << std::endl; ///////////////////////////////cout
  std::cout<< "        HPWL delta map size: " << hpDelta.size() << std::endl; ///////////////////////////////cout
  std::cout<< "        STWL delta map size: " << stDelta.size() << std::endl; ///////////////////////////////cout

  std::sort(hpDelta.begin(), hpDelta.end()); //sort delta vectors
  std::sort(stDelta.begin(), stDelta.end());

  for(auto data: hpDelta){ //report cell HPWL deltas
    auto integer = data.first;
    auto text = data.second;
    std::cout<<" HPWL Delta: "<< integer <<" cell name: " << text <<std::endl; ///////////////////////////////cout
  } //for

  for(auto data: stDelta){ //report cell STWL deltas
    auto integer = data.first;
    auto text = data.second;
    std::cout<<" STWL Delta: "<< integer <<" cell name: " << text <<std::endl; ///////////////////////////////cout
  } //for
  
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

#if(0)
{

  odb::dbBlock *block = db_->getChip()->getBlock(); //pega o bloco
  auto cellNumber = db_->getChip()->getBlock()->getInsts().size();

  std::cout<<"No. of cells in block: "<< cellNumber << std::endl;

  
  std::map <std::string, std::pair<int,int>> netWlLookup; //nets e hpwl, wl - por enquanto inutil
  std::map <std::string, int> netDelta; //nets e deltas
  std::map <std::string, int> netRatio; //nets e razoes
  //std::vector <std::pair<int,std::string>> cellDelta; //deltas e celulas
  //std::vector <std::pair<int,std::string>> cellRatio; //vetor de razoes delta/n.pinos
  

  for (auto net: block->getNets()){ //cálculo do delta hpwl-wl das nets

      
    auto netName = net->getName(); //pega o nome desta net
    auto pinCount = net->getITermCount(); //num de pinos da net

    std::cout<<"      NetName: "<< netName <<"      NetPinCount: "<< pinCount << std::endl;

    int hpwl=0, wl=0;
    //int dRatio=0;

    netWlLookup[netName] = std::make_pair(hpwl,wl);
    
    hpwl = calc_HPWL(net);
    netWlLookup[netName].first = hpwl; //insere hpwl no pair

    wl = grt_->computeNetWirelength(net); //transformei esse metodo pra public - WL da net
    std::cout<<"               WL: "<< wl << std::endl; //calculo do wirelength da net
    netWlLookup[netName].second = wl; //insere wl no pair

    int netDelta = wl - hpwl; //calculo do delta da net/pino
    std::cout<<"        Net Delta: "<< netDelta << std::endl;

    if(pinCount && netDelta >0) dRatio = netDelta / pinCount; // calc razao delta/pins
    else dRatio = 0;


    delta.push_back(std::make_pair(netDelta,netName));
    ratio.push_back(std::make_pair(dRatio,netName));

    std::cout<< "            cell map size: " << netWlLookup.size() << std::endl;
    std::cout<< "           ratio map size: " << ratio.size() << std::endl;
    std::cout<< "           delta map size: " << delta.size() << std::endl;

  } //for

  std::sort(delta.begin(),delta.end());
  std::sort(ratio.begin(),ratio.end());

  for(auto data: delta){
    auto inteiro = data.first;
    auto text = data.second;
    std::cout<<" Delta: "<< inteiro <<" Net name: " << text <<std::endl;
  }

  for(auto data: ratio){
    auto inteiro = data.first;
    auto text = data.second;
    std::cout<<" Ratio: "<< inteiro <<" Net name: " << text <<std::endl;
  }


} //metodo
#endif

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
