#ifndef DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleStreamSoA_h
#define DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleStreamSoA_h

#include "DataFormats/SoATemplate/interface/SoALayout.h"

// One row per module: where its chip streams sit in the flat byte buffer, in 32-bit words
GENERATE_SOA_LAYOUT(Phase2ITModuleStreamLayout,
                    SOA_COLUMN(uint32_t, detId),
                    SOA_COLUMN(uint32_t, wordBegin),  // first chip header
                    SOA_COLUMN(uint32_t, wordEnd),    // one past the module's last word, which bounds its chips
                    SOA_COLUMN(uint16_t, moduleId),   // TrackerGeometry detUnit index
                    SOA_COLUMN(uint8_t, subtype),
                    SOA_COLUMN(uint8_t, nChips),      // chips of its subtype; a chip's ID is its place in the module
                    SOA_COLUMN(uint8_t, badOffset),   // offset out of range, so the module was dropped
                    SOA_COLUMN(uint8_t, extraChips))  // more chip headers than its subtype has chips

using Phase2ITModuleStreamSoA = Phase2ITModuleStreamLayout<>;
using Phase2ITModuleStreamSoAView = Phase2ITModuleStreamSoA::View;
using Phase2ITModuleStreamSoAConstView = Phase2ITModuleStreamSoA::ConstView;

// Four rows per module, row 4m + k for chip k of module m: where each chip's stream sits in the flat byte
// buffer, found from the chip headers. A row without a chip has zero bits.
GENERATE_SOA_LAYOUT(Phase2ITChipStreamLayout,
                    SOA_COLUMN(uint32_t, payloadWord),  // first payload word, after the chip header
                    SOA_COLUMN(uint32_t, bitLen),
                    SOA_COLUMN(uint8_t, overrun))  // payload past the module's end, so the chip is read as empty

using Phase2ITChipStreamSoA = Phase2ITChipStreamLayout<>;
using Phase2ITChipStreamSoAView = Phase2ITChipStreamSoA::View;
using Phase2ITChipStreamSoAConstView = Phase2ITChipStreamSoA::ConstView;

GENERATE_SOA_LAYOUT(Phase2ITRawBytesLayout, SOA_COLUMN(uint8_t, byte))

using Phase2ITRawBytesSoA = Phase2ITRawBytesLayout<>;
using Phase2ITRawBytesSoAView = Phase2ITRawBytesSoA::View;
using Phase2ITRawBytesSoAConstView = Phase2ITRawBytesSoA::ConstView;

#endif  // DataFormats_Phase2ITBitStreamSoA_interface_Phase2ITModuleStreamSoA_h
