#include "tut/Tutorial.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"

#include <iostream>
#include <limits>

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
  using std::string, std::to_string;

  auto block = db_->getChip()->getBlock();
  for(auto net : block->getNets())
  {
    string pins;

    using std::numeric_limits;
    int min_x = numeric_limits<int>::max(),
        min_y = numeric_limits<int>::max(),
        max_x = numeric_limits<int>::min(),
        max_y = numeric_limits<int>::min();
    for (auto iterm : net->getITerms()) {
      int x, y; 
      const bool pinExist = iterm->getAvgXY(&x, &y);
      if (pinExist) {
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;

        pins += string("(") + to_string(x) + ", " + to_string(y) + "); ";
      }
    }

    int hpwl;
    if (net->getITerms().size() == 0) {
      hpwl = 0;
    } else {
      hpwl = (max_x - min_x) + (max_y - min_y); 
    }

    string s;
    s += string("Net = ") + net->getName() + "\n";
    s += string("HPWL = ") + to_string(hpwl) + "\n";
    s += string("(min_x, min_y) = ") + "(" + to_string(min_x) + ", " + to_string(min_y) + ")" + "\n";
    s += string("(max_x, max_y) = ") + "(" + to_string(max_x) + ", " + to_string(max_y) + ")" + "\n";
    s += string("Pins = ") + pins + "\n\n";

//    std::cout << s;
    logger_->report(s);
  }
}

Tutorial::~Tutorial()
{
  //clear();
}

}
