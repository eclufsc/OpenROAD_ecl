include("openroad")

project(tut)

add_subdirectory(src/ParquetFP2)

swig_lib(NAME      tut
         NAMESPACE tut
         I_FILE    src/Tutorial.i
         SCRIPTS   src/Tutorial.tcl
)

target_sources(tut
  PRIVATE
    src/Tutorial.cpp
    src/MakeTutorial.cpp
    src/Partition.cpp
    src/graphics.cpp
)

target_include_directories(tut
  PUBLIC
    include
)

target_link_libraries(tut
    utl
    odb
    OpenSTA
    ParquetFP2
    dbSta
    gui
    ppl
)

messages(
  TARGET tut
)
