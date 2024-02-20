include("openroad")

project(cng)

swig_lib(NAME      cng
         NAMESPACE cng
         I_FILE    src/Congestion.i
         SCRIPTS   src/Congestion.tcl
)

target_sources(cng
  PRIVATE
    src/Congestion.cpp
    src/MakeCongestion.cpp
)

target_include_directories(cng
  PUBLIC
    include
)

target_link_libraries(cng
    utl
    gui
    odb
    OpenSTA
    drw
)

messages(
  TARGET cng
)
