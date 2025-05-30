# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

add_library(hemelb_net OBJECT
  MpiEnvironment.cc MpiError.cc
  MpiCommunicator.cc MpiGroup.cc MpiFile.cc
  IteratedAction.cc BaseNet.cc 
  IOCommunicator.cc
  mixins/pointpoint/CoalescePointPoint.cc
  mixins/pointpoint/SeparatedPointPoint.cc
  mixins/pointpoint/ImmediatePointPoint.cc
  mixins/gathers/SeparatedGathers.cc 
  mixins/gathers/ViaPointPointGathers.cc
  mixins/alltoall/SeparatedAllToAll.cc
  mixins/alltoall/ViaPointPointAllToAll.cc
  mixins/StoringNet.cc
  phased/StepManager.cc)
configure_file (
  "${PROJECT_SOURCE_DIR}/net/BuildInfo.h.in"
  "${PROJECT_BINARY_DIR}/net/BuildInfo.h"
  )
