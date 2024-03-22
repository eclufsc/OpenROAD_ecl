#include "tut/Tutorial.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "grt/GlobalRouter.h"
#include "stt/SteinerTreeBuilder.h"


#include <iostream>

int
getTreeWl(const stt::Tree &tree)
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
  stt_{ord::OpenRoad::openRoad()->getSteinerTreeBuilder()},
  grt_{ord::OpenRoad::openRoad()->getGlobalRouter()}
{
}

void
Tutorial::printHello()

{

  odb::dbBlock *block = db_->getChip()->getBlock(); //pega o bloco
  auto cellNumber = db_->getChip()->getBlock()->getInsts().size();

  block->setDrivingItermsforNets(); //setar os drivers das nets (?)
  //stt_->setAlpha(0.0); // any attempt setting Alpha causes segmentation fault crash

  std::cout<<"No. of cells in block: "<< cellNumber << std::endl;

  
  std::map <std::string, std::pair<int,int>> netWlLookup; //mapa de nets e hpwl, wl
  std::vector<std::pair<int,std::string>> delta; //mapa de nets e deltas
  std::vector<std::pair<int,std::string>> ratio; //mapa de razoes delta/n.pinos
  

  for (auto net: block->getNets()){ //cálculo do delta hpwl-wl de uma net

      
    auto netName = net->getName(); //pega o nome desta net
    auto pinCount = net->getITermCount(); //num de pinos da net

    std::cout<<"      NetName: "<< netName <<"      NetPinCount: "<< pinCount << std::endl;

    auto tree = buildSteinerTree(net); //faz a steiner tree da net
    int stwl = getTreeWl(tree);
    std::cout<<"             STWL: "<< stwl << std::endl; //calculo do steiner wirelength da net

    int hpwl=0, wl=0;

    netWlLookup[netName] = std::make_pair(hpwl,wl);
    
    hpwl = calc_HPWL(net);
    netWlLookup[netName].first = hpwl; //insere hpwl no pair

    wl = grt_->computeNetWirelength(net); //transformei esse metodo pra public - WL da net
    std::cout<<"               WL: "<< wl << std::endl; //calculo do wirelength da net
    netWlLookup[netName].second = wl; //insere wl no pair

    int netDelta = wl - hpwl; //calculo do delta da net/pino
    std::cout<<"        Net Delta: "<< netDelta << std::endl;

    delta.push_back(std::make_pair(netDelta,netName));

    std::cout<< "            cell map size: " << netWlLookup.size() << std::endl;
    std::cout<< "           delta map size: " << delta.size() << std::endl;

  } //for

  std::sort(delta.begin(), delta.end());

  for(auto data: delta){
    auto inteiro = data.first;
    auto text = data.second;
    std::cout<<" Delta: "<< inteiro <<" Net name: " << text <<std::endl;
  }

} //metodo

stt::Tree
Tutorial::buildSteinerTree(odb::dbNet * net)
{
  //std::cout<<"Branch Count :\n";
  //skip PG Nets (double check for clock?)

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
      std::cout<<"foi"<< std::endl; //apagar
      //std::cout<<"x: "<< x << std::endl; //apagar
      //std::cout<<"y: "<< y << std::endl; //apagar
    }
  }
  std::cout<<"root index: "<<rootIndex<< std::endl; //apagar
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


  #if(0)
  int xll = std::numeric_limits<int>::max();
  int yll = std::numeric_limits<int>::max();
  int xur = std::numeric_limits<int>::min();
  int yur = std::numeric_limits<int>::min();
  for(auto iterm : net->getITerms())
  {
    int x=0, y=0;
    iterm->getAvgXY(&x, &y);
            
    xur = std::max(xur, x);
    yur = std::max(yur, y);
    xll = std::min(xll, x);
    yll = std::min(yll, y);
  }
  int width = std::abs(xur-xll);
  int height = std::abs(yur-yll);
          
  int hpwl = width + height; //calculo do hpwl da net/pino
  std::cout<<"             HPWL: "<< hpwl << std::endl;
  return hpwl;
 #endif
}

void
Tutorial::printCells()
{
  
  
  
  
  
  
  
  
  
  /*
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
