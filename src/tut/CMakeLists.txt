include("openroad")

project(tut)

swig_lib(NAME      tut
         NAMESPACE tut
         I_FILE    src/Tutorial.i
         SCRIPTS   src/Tutorial.tcl
)

target_sources(tut
  PRIVATE
    src/Tutorial.cpp
    src/MakeTutorial.cpp
)

set_source_files_properties(src/Tutorial.cpp PROPERTIES COMPILE_FLAGS "-g -O0")

target_include_directories(tut
  PUBLIC
    include
)

target_link_libraries(tut
    utl
    odb
    OpenSTA
    grt
    stt
)

messages(
  TARGET tut
)
