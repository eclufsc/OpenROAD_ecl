#############################################################################
##
## Copyright (c) 2019, The Regents of the University of California
## All rights reserved.
##
## BSD 3-Clause License
##
## Redistribution and use in source and binary forms, with or without
## modification, are permitted provided that the following conditions are met:
##
## * Redistributions of source code must retain the above copyright notice, this
##   list of conditions and the following disclaimer.
##
## * Redistributions in binary form must reproduce the above copyright notice,
##   this list of conditions and the following disclaimer in the documentation
##   and/or other materials provided with the distribution.
##
## * Neither the name of the copyright holder nor the names of its
##   contributors may be used to endorse or promote products derived from
##   this software without specific prior written permission.
##
## THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
## AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
## IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
## ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
## LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
## CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
## SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
## INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
## CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
## ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
## POSSIBILITY OF SUCH DAMAGE.

include("openroad")

add_subdirectory(src/ParquetFP2)

swig_lib(NAME      mpl3
         NAMESPACE mpl3
         I_FILE    src/MacroPlacer3.i
         SCRIPTS   src/MacroPlacer3.tcl
)

target_sources(mpl3
  PRIVATE
    src/Partition.cpp
    src/MacroPlacer3.cpp
    src/MakeMacroPlacer.cpp
    src/graphics.cpp
)

target_include_directories(mpl3
  PUBLIC
    include
)

target_link_libraries(mpl3
  PRIVATE
    ParquetFP2
    dbSta
    OpenSTA
    odb
    utl
    gui
    ppl
)

messages(
  TARGET mpl3
)

if (Python3_FOUND AND BUILD_PYTHON)
  swig_lib(NAME          mpl3_py
           NAMESPACE     mpl3
           LANGUAGE      python
           I_FILE        src/MacroPlacer3-py.i
           SWIG_INCLUDES ${PROJECT_SOURCE_DIR}/include/mpl3
                         ${ODB_HOME}/src/swig/common
                         ${ODB_HOME}/src/swig/python
           SCRIPTS       ${CMAKE_CURRENT_BINARY_DIR}/mpl3_py.py
  )

  target_link_libraries(mpl3_py
    PUBLIC
      mpl3
      OpenSTA
      odb
      ppl
  )

endif()
