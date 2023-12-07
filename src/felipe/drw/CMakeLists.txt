include("openroad")

project(drw)

swig_lib(NAME      drw
         NAMESPACE drw
         I_FILE    src/Drawing.i
         SCRIPTS   src/Drawing.tcl
)

target_sources(drw
  PRIVATE
  src/Drawing.cpp
  src/MakeDrawing.cpp
)

target_include_directories(drw
  PUBLIC
    include
)

target_link_libraries(drw
    utl
    odb
    gui
    OpenSTA
)

messages(
  TARGET drw
)
