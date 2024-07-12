include("openroad")

project(feps_subs)

swig_lib(NAME      feps_subs
         NAMESPACE feps_subs
         I_FILE    src/FepsSubstitute.i
         SCRIPTS   src/FepsSubstitute.tcl
)

target_sources(feps_subs
  PRIVATE
    src/FepsSubstitute.cpp
    src/MakeFepsSubstitute.cpp
)

target_include_directories(feps_subs
  PUBLIC
    include
)

target_link_libraries(feps_subs
    utl
    odb
    OpenSTA
)

messages(
  TARGET feps_subs
)
