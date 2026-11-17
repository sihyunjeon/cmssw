#ifndef DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleMapSoA_h
#define DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleMapSoA_h

#include "DataFormats/SoATemplate/interface/SoABlocks.h"
#include "DataFormats/SoATemplate/interface/SoALayout.h"

// Device-side version of SLinkModuleMap
GENERATE_SOA_LAYOUT(Phase2ITModuleMapLayout,
                    SOA_COLUMN(uint16_t, fedIdx),  // index of the FED
                    SOA_COLUMN(uint32_t, detId),
                    SOA_COLUMN(uint8_t, subtype),   // Module_SubType, needed for the chip indexing
                    SOA_COLUMN(uint8_t, nChips),    // chips on a module of that subtype (ChipModuleMap::nChips)
                    SOA_COLUMN(uint16_t, geomIdx))  // TrackerGeometry detUnit index

GENERATE_SOA_LAYOUT(Phase2ITFedMapLayout, SOA_COLUMN(int32_t, modStart), SOA_COLUMN(int32_t, fedId))

GENERATE_SOA_BLOCKS(Phase2ITModuleMapBlocksLayout,
                    SOA_BLOCK(modules, Phase2ITModuleMapLayout),
                    SOA_BLOCK(feds, Phase2ITFedMapLayout))

using Phase2ITModuleMapSoA = Phase2ITModuleMapBlocksLayout<>;
using Phase2ITModuleMapSoAView = Phase2ITModuleMapSoA::View;
using Phase2ITModuleMapSoAConstView = Phase2ITModuleMapSoA::ConstView;

#endif  // DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleMapSoA_h
