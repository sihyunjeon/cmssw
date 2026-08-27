#ifndef DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleMapSoA_h
#define DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleMapSoA_h

#include "DataFormats/SoATemplate/interface/SoALayout.h"

// Device-side counterpart of SLinkModuleMap: the cabling and geometry derived
// tables the unpacking kernels index. Built once per IOV by
// Phase2ITModuleMapESProducer, since it follows the conditions and not the event.
//
// Modules are ordered FED by FED, so the modules of FED f are the rows
// [modStart(f), modStart(f + 1)) of the module layout.
GENERATE_SOA_LAYOUT(Phase2ITModuleMapLayout,
                    SOA_COLUMN(uint16_t, fedIdx),   // index of the owning FED, into the FED layout
                    SOA_COLUMN(uint32_t, detId),
                    SOA_COLUMN(uint8_t, subtype),   // Module_SubType, keys the chip quadrant table
                    SOA_COLUMN(uint16_t, geomIdx))  // TrackerGeometry detUnit index

using Phase2ITModuleMapSoA = Phase2ITModuleMapLayout<>;
using Phase2ITModuleMapSoAView = Phase2ITModuleMapSoA::View;
using Phase2ITModuleMapSoAConstView = Phase2ITModuleMapSoA::ConstView;

// One row per FED plus a trailing row, so modStart(nFeds) closes the last range.
// fedId on that trailing row is unset.
GENERATE_SOA_LAYOUT(Phase2ITFedMapLayout, SOA_COLUMN(int32_t, modStart), SOA_COLUMN(int32_t, fedId))

using Phase2ITFedMapSoA = Phase2ITFedMapLayout<>;
using Phase2ITFedMapSoAView = Phase2ITFedMapSoA::View;
using Phase2ITFedMapSoAConstView = Phase2ITFedMapSoA::ConstView;

#endif  // DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleMapSoA_h
