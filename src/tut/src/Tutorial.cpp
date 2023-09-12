#include "tut/Tutorial.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"

#include <iostream>
#include <math.h>

/*
 * How to use:
 * Most of the ODB functionaly can be understood
 * looking at "odb/db.h" File
*/

namespace tut {

Tutorial::Tutorial() :
  db_{ord::OpenRoad::openRoad()->getDb()},
  logger_{ord::OpenRoad::openRoad()->getLogger()}
{
}

void
Tutorial::printHello()
{
  logger_->report("Hello World.");
}

void
Tutorial::printCells()
{
  //You can also print things in the logger if you want :)
  std::cout<<"Printing all cell names:"<<std::endl;
  logger_->report("Printing all cell names:");
  auto block = db_->getChip()->getBlock();
  for(auto inst : block->getInsts())
  {
    std::cout<<inst->getName()<<std::endl;
    logger_->report(inst->getName());
  }
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
    for(auto iterm : net->getITerms())
    {
      auto cell = iterm->getInst();
      auto cellName = cell->getName();
      auto std_pin = iterm->getMTerm();
      auto pinName = std_pin->getName();
      int x=0, y=0;
      const bool pinExist = iterm->getAvgXY(&x, &y);
      if(pinExist)
      {
        const std::string pin_str = "Pin: "+iterm->getInst()->getName()+
                                    ":"+iterm->getMTerm()->getName()+
                                    " x:"+std::to_string(x)+" y:"+std::to_string(y);
      }
    }
  }
}

void
Tutorial::printHPWLs()
{
  //TODO
  //Challenge: Traverse all nets printing the total HPWL
  std::cout<<"Total HPWL: "<<std::end;
  auto block = db_->getChip()->getBlock();
  double HPWL=0;
  int HX,HY,LX,LY;
  int x, y;
  for(auto net : block->getNets()) {
    HX=-1; HY=-1; LX=-1; LY=-1;
    for(auto iterm : net->getITerms()) {
      x=0; y=0;
      iterm->getAvgXY(&x,&y);
      if((x<LX)||(LX==-1))
        LX = x;
      if((x>HX)||(LX==-1))
        HX = x;
      if((y<LY)||(LY==-1))
        LY = y;
      if((y>HY)||(HY==-1))
        HY = y;
    }
    HPWL+=((HX-LX)+(HY-LY));
  }
  std::cout<<std::to_string(HPWL)<<std::endl;
  logger_->report("Total HPWL: "+std::to_string(HPWL));
}

Tutorial::~Tutorial()
{
  //clear();
}

}
