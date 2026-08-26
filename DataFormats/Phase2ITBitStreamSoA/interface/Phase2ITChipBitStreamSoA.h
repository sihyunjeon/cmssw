#ifndef DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITChipBitStreamSoA_h
#define DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITChipBitStreamSoA_h

#include "DataFormats/SoATemplate/interface/SoALayout.h"

// Device-side counterpart of edmNew::DetSetVector<Phase2ITChipBitStream>:
// one row per chip, pointing into a flat byte buffer of the raw FED bodies
// (Phase2ITRawBytesSoA). The chip's RD53 stream is bitLen bits starting at
// bitOffset, MSB first.
GENERATE_SOA_LAYOUT(Phase2ITChipBitStreamLayout,
                    SOA_COLUMN(uint32_t, detId),
                    SOA_COLUMN(uint32_t, bitOffset),
                    SOA_COLUMN(uint32_t, bitLen),
                    SOA_COLUMN(uint16_t, moduleId),  // TrackerGeometry detUnit index
                    SOA_COLUMN(uint8_t, chipId),
                    SOA_COLUMN(uint8_t, subtype))

using Phase2ITChipBitStreamSoA = Phase2ITChipBitStreamLayout<>;
using Phase2ITChipBitStreamSoAView = Phase2ITChipBitStreamSoA::View;
using Phase2ITChipBitStreamSoAConstView = Phase2ITChipBitStreamSoA::ConstView;

GENERATE_SOA_LAYOUT(Phase2ITRawBytesLayout, SOA_COLUMN(uint8_t, byte))

using Phase2ITRawBytesSoA = Phase2ITRawBytesLayout<>;
using Phase2ITRawBytesSoAView = Phase2ITRawBytesSoA::View;
using Phase2ITRawBytesSoAConstView = Phase2ITRawBytesSoA::ConstView;

#endif  // DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITChipBitStreamSoA_h
