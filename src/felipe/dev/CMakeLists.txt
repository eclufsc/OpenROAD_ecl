include("openroad")

project(dev)

swig_lib(NAME      dev
         NAMESPACE dev
         I_FILE    src/Dev.i
         SCRIPTS   src/Dev.tcl
)

target_sources(dev
  PRIVATE
    src/Dev.cpp
    src/MakeDev.cpp
)

target_include_directories(dev
  PUBLIC
    include
)

target_link_libraries(dev
    utl
    grt
    odb
    OpenSTA
)

messages(
  TARGET dev
)
