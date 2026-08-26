// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      phase2it unpacking kernels
// Description: Count/fill kernel pairs for the two device unpacking stages.
//              Each stage decodes twice: once to size the output, once to fill
//              it, since a kernel cannot grow its output collection.
//
// Author: Si Hyun Jeon, shjeon@cern.ch
//

#include <alpaka/alpaka.hpp>

#include "DataFormats/SiPixelDetId/interface/PixelChannelIdentifier.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "Phase2ITDecode.h"
#include "Phase2ITUnpackKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::phase2it {

  namespace {
    // Start of the FED body owning module m, inside the concatenated buffer.
    ALPAKA_FN_ACC inline const uint8_t* fedBytes(const uint8_t* bytes, const ModuleNav& nav, int m) {
      return bytes + nav.fedWordBase[nav.modFedIdx[m]] * 4;
    }
    // Word span of module m, taken from its FED's offset block.
    ALPAKA_FN_ACC inline ModuleSpan spanOf(const uint8_t* bytes, const ModuleNav& nav, int m) {
      const int f = nav.modFedIdx[m];
      return moduleSpan(fedBytes(bytes, nav, m),
                        nav.fedSizeWords[f],
                        nav.fedModStart[f + 1] - nav.fedModStart[f],
                        m - nav.fedModStart[f]);
    }
  }  // namespace

  // Stage 1: raw FED bodies -> per-chip index into those bytes.

  // FIXME chips per module is static (ModuleInfo.nChips), so this count could be
  // built once per IOV instead of every event, saving a kernel and a host sync.
  struct ChipCountKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, const uint8_t* bytes, ModuleNav nav, uint32_t* counts) const {
      for (auto m : cms::alpakatools::uniform_elements(acc, nav.nModules)) {
        uint32_t n = 0;
        forEachChip(fedBytes(bytes, nav, m), spanOf(bytes, nav, m), [&](int, int, uint32_t) { ++n; });
        counts[m] = n;
      }
    }
  };

  struct ChipFillKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  const uint8_t* bytes,
                                  ModuleNav nav,
                                  const uint32_t* offsets,
                                  Phase2ITChipBitStreamSoAView chips) const {
      for (auto m : cms::alpakatools::uniform_elements(acc, nav.nModules)) {
        // bitOffset is relative to the whole buffer, so stage 2 needs no navigation
        const uint8_t* fb = fedBytes(bytes, nav, m);
        const uint32_t fedByteBase = nav.fedWordBase[nav.modFedIdx[m]] * 4;
        uint32_t row = offsets[m];
        forEachChip(fb, spanOf(bytes, nav, m), [&](int chipId, int payloadWord, uint32_t bitLen) {
          auto c = chips[row++];
          c.detId() = nav.modDetId[m];
          c.bitOffset() = (fedByteBase + payloadWord * 4) * 8;
          c.bitLen() = bitLen;
          c.moduleId() = nav.modGeomIdx[m];
          c.chipId() = uint8_t(chipId);
          c.subtype() = nav.modSubtype[m];
        });
      }
    }
  };

  void runChipCountKernel(Queue& queue, const uint8_t* bytes, const ModuleNav& nav, uint32_t* counts) {
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(nav.nModules, 128), 128);
    alpaka::exec<Acc1D>(queue, wd, ChipCountKernel{}, bytes, nav, counts);
  }

  void runChipFillKernel(Queue& queue,
                         const uint8_t* bytes,
                         const ModuleNav& nav,
                         const uint32_t* offsets,
                         Phase2ITChipBitStreamSoAView chips) {
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(nav.nModules, 128), 128);
    alpaka::exec<Acc1D>(queue, wd, ChipFillKernel{}, bytes, nav, offsets, chips);
  }

  // Stage 2: per-chip bit streams -> digis.

  struct DigiCountKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  const uint8_t* bytes,
                                  Phase2ITChipBitStreamSoAConstView chips,
                                  bool dropTot,
                                  uint32_t* counts) const {
      for (auto c : cms::alpakatools::uniform_elements(acc, chips.metadata().size())) {
        uint32_t n = 0;
        decodeChip(BitReader{bytes + (chips[c].bitOffset() >> 3), chips[c].bitLen()},
                   dropTot,
                   [&](int, int, int, int) { ++n; });
        counts[c] = n;
      }
    }
  };

  struct DigiFillKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  const uint8_t* bytes,
                                  Phase2ITChipBitStreamSoAConstView chips,
                                  bool dropTot,
                                  bool keepMode,
                                  const uint32_t* offsets,
                                  SiPixelDigisSoAView digis) const {
      constexpr int kRowShift = PixelChannelIdentifier::thePacking.row_shift;
      constexpr int kColShift = PixelChannelIdentifier::thePacking.column_shift;
      constexpr int kAdcShift = PixelChannelIdentifier::thePacking.adc_shift;
      // The collection carries one spare row past the digis; zero it once.
      if (cms::alpakatools::once_per_grid(acc)) {
        const int last = digis.metadata().size() - 1;
        digis[last].clus() = 0;
        digis[last].pdigi() = 0;
        digis[last].rawIdArr() = 0;
        digis[last].adc() = 0;
        digis[last].xx() = 0;
        digis[last].yy() = 0;
        digis[last].moduleId() = 0;
      }
      // offsets[] gives each chip a private output range, so no atomics are needed
      for (auto c : cms::alpakatools::uniform_elements(acc, chips.metadata().size())) {
        const auto chip = chips[c];
        const int subtype = chip.subtype();
        const int chipId = chip.chipId();
        uint32_t cursor = offsets[c];
        decodeChip(BitReader{bytes + (chip.bitOffset() >> 3), chip.bitLen()},
                   dropTot,
                   [&](int ccol, int qrow, int i, int adc) {
                     int row, col;
                     hitToRowCol(subtype, chipId, ccol, qrow, i, keepMode, row, col);
                     auto d = digis[cursor++];
                     d.clus() = 0;
                     d.pdigi() =
                         (uint32_t(row) << kRowShift) | (uint32_t(col) << kColShift) | (uint32_t(adc) << kAdcShift);
                     d.rawIdArr() = chip.detId();
                     d.adc() = uint16_t(adc);
                     d.xx() = uint16_t(row);
                     d.yy() = uint16_t(col);
                     d.moduleId() = chip.moduleId();
                   });
      }
    }
  };

  void runDigiCountKernel(
      Queue& queue, const uint8_t* bytes, Phase2ITChipBitStreamSoAConstView chips, bool dropTot, uint32_t* counts) {
    const int n = chips.metadata().size();
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(n, 128), 128);
    alpaka::exec<Acc1D>(queue, wd, DigiCountKernel{}, bytes, chips, dropTot, counts);
  }

  void runDigiFillKernel(Queue& queue,
                         const uint8_t* bytes,
                         Phase2ITChipBitStreamSoAConstView chips,
                         bool dropTot,
                         bool keepMode,
                         const uint32_t* offsets,
                         SiPixelDigisSoAView digis) {
    const int n = chips.metadata().size();
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(n, 128), 128);
    alpaka::exec<Acc1D>(queue, wd, DigiFillKernel{}, bytes, chips, dropTot, keepMode, offsets, digis);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::phase2it
