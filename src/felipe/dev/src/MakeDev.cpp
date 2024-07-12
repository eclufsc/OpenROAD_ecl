#include "dev/MakeDev.h"
#include "dev/Dev.h"
#include "ord/OpenRoad.hh"
#include "sta/StaMain.hh"

namespace sta {
    // Tcl files encoded into strings.
    extern const char* dev_tcl_inits[];
}  // namespace sta

//Rule: Dev class -> Dev_Init
//So, the module name in .i have to equal the Class name,
//although .i is considered case insensitive.
extern "C" {
    extern int Dev_Init(Tcl_Interp* interp);
}

//All these three functions are being used in /src/OpenRoad.cc
namespace ord {
    dev::Dev* makeDev() {
        return new dev::Dev();
    }

    //This funcion will bind the calls between .tcl and .i files
    void initDev(OpenRoad *openroad) {
        Tcl_Interp* tcl_interp = openroad->tclInterp();
        // Define swig TCL commands.
        Dev_Init(tcl_interp);
        sta::evalTclInit(tcl_interp, sta::dev_tcl_inits);
    }

    void deleteDev(dev::Dev *dev) {
        delete dev;
    }
}
