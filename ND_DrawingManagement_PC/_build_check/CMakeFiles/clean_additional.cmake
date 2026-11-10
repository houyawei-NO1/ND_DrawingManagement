# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\ND_DrawingManagement_PC_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\ND_DrawingManagement_PC_autogen.dir\\ParseCache.txt"
  "ND_DrawingManagement_PC_autogen"
  )
endif()
