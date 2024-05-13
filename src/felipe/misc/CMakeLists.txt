include("openroad")

project(misc)

swig_lib(NAME      misc
         NAMESPACE misc
         I_FILE    src/Misc.i
         SCRIPTS   src/Misc.tcl
)

target_sources(misc
  PRIVATE
    src/Misc.cpp
    src/MakeMisc.cpp
)

set_source_files_properties(src/Misc.cpp PROPERTIES COMPILE_FLAGS "-g -O0")

target_include_directories(misc
  PUBLIC
    include
)

target_link_libraries(misc
    utl
    odb
    gui
    OpenSTA
)

messages(
  TARGET misc
)
