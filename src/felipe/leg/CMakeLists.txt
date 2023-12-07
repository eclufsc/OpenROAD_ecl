include("openroad")

project(leg)

swig_lib(NAME      leg
         NAMESPACE leg
         I_FILE    src/Legalizer.i
         SCRIPTS   src/Legalizer.tcl
)

target_sources(leg
  PRIVATE
    src/Legalizer.cpp
    src/MakeLegalizer.cpp
)

target_include_directories(leg
  PUBLIC
    include
)

target_link_libraries(leg
    utl
    odb
    gui
    OpenSTA
)

messages(
  TARGET leg
)
