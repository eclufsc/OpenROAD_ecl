%{
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "tut/Tutorial.h"

namespace ord {
tut::Tutorial* getTutorial(); // Defined in OpenRoad.i
OpenRoad *getOpenRoad(); // Defined in OpenRoad.i
}  // namespace ord

using ord::getTutorial;
using tut::Tutorial;
%}

%inline %{

namespace tut {

void
print()
{
  Tutorial* tutorial = getTutorial();
  tutorial->printHello();
}

void
printCells()
{
  Tutorial* tutorial = getTutorial();
  tutorial->printCells();
}

void
printNets()
{
  Tutorial* tutorial = getTutorial();
  tutorial->printNets();
}

void
printPins()
{
  Tutorial* tutorial = getTutorial();
  tutorial->printPins();
}

void
printHPWLs()
{
  Tutorial* tutorial = getTutorial();
  tutorial->printHPWLs();
}

void
test()
{
  Tutorial* tutorial = getTutorial();
  tutorial->test();
}

void
set_halo(double halo_v, double halo_h)
{
  Tutorial* macro_placer = getTutorial();
  macro_placer->setHalo(halo_v, halo_h);
}

void
set_channel(double channel_v, double channel_h)
{
  Tutorial* macro_placer = getTutorial();
  macro_placer->setChannel(channel_v, channel_h); 
}

void
set_fence_region(double lx, double ly, double ux, double uy)
{
  Tutorial* macro_placer = getTutorial();
  macro_placer->setFenceRegion(lx, ly, ux, uy); 
}

void
set_snap_layer(odb::dbTechLayer *snap_layer)
{
  Tutorial* macro_placer = getTutorial();
  macro_placer->setSnapLayer(snap_layer);
}

void
place_macros_corner_min_wl()
{
  Tutorial* macro_placer = getTutorial();
  macro_placer->placeMacrosCornerMinWL2(); 
} 

void
place_macros_corner_max_wl()
{
  Tutorial* macro_placer = getTutorial();
  macro_placer->placeMacrosCornerMaxWl2(); 
} 

void set_debug_cmd(bool partitions)
{
  Tutorial* macro_placer = getTutorial();
  macro_placer->setDebug(partitions);
}

} // namespace

%} // inline
