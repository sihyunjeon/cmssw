#ifndef DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleMapSoA_h
#define DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleMapSoA_h

#include "DataFormats/SoATemplate/interface/SoALayout.h"

// Device-side version of SLinkModuleMap
GENERATE_SOA_LAYOUT(Phase2ITModuleMapLayout,
                    SOA_COLUMN(uint16_t, fedIdx),  // index of the FED
                    SOA_COLUMN(uint32_t, detId),
                    SOA_COLUMN(uint8_t, subtype),   // Module_SubType, needed for the chip indexing
                    SOA_COLUMN(uint16_t, geomIdx))  // TrackerGeometry detUnit index

using Phase2ITModuleMapSoA = Phase2ITModuleMapLayout<>;
using Phase2ITModuleMapSoAView = Phase2ITModuleMapSoA::View;
using Phase2ITModuleMapSoAConstView = Phase2ITModuleMapSoA::ConstView;

GENERATE_SOA_LAYOUT(Phase2ITFedMapLayout, SOA_COLUMN(int32_t, modStart), SOA_COLUMN(int32_t, fedId))

using Phase2ITFedMapSoA = Phase2ITFedMapLayout<>;
using Phase2ITFedMapSoAView = Phase2ITFedMapSoA::View;
using Phase2ITFedMapSoAConstView = Phase2ITFedMapSoA::ConstView;

#endif  // DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleMapSoA_h
