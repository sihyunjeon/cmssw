#ifndef DataFormats_Phase2TrackerDigi_Phase2ITChip_H
#define DataFormats_Phase2TrackerDigi_Phase2ITChip_H
#include <vector>
#include <utility>
#include <string>
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITQCore.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITDigiHit.h"

class Phase2ITChip {
  // Quarter cores collected into a chip (only active quarter cores with hits gets collected)

public:
  Phase2ITChip(int rocnum, const std::vector<Phase2ITDigiHit> hl, uint32_t detId);

  unsigned int size();
  int rocnum() const { return rocnum_; }
  uint32_t detId() const { return detId_; }

  std::vector<Phase2ITQCore> getOrganizedQCores();
  std::vector<bool> getChipCode();

  static int encodeQCoreIndex(int row, int col);
  static std::pair<int, int> decodeQCoreIndex(int index);

  static std::pair<int, int> getGlobalPixelCoordinate(
      int chipId, int qcoreCol, int qcoreRow, int localCol, int localRow);

  static constexpr int kColsPerROC = 54;
  static constexpr int kRowsPerChip = 672;
  static constexpr int kColsPerChip = 216;

private:
  std::vector<Phase2ITDigiHit> hitList_;
  int rocnum_;
  uint32_t detId_;

  std::pair<int, int> getQCorePos(Phase2ITDigiHit hit);

  Phase2ITQCore getQCoreFromHit(Phase2ITDigiHit pixel);
  std::vector<Phase2ITQCore> remDuplicates(std::vector<Phase2ITQCore> qcores);
  std::vector<Phase2ITQCore> organizeQCores(std::vector<Phase2ITQCore> qcores);
};

#endif  // DataFormats_Phase2TrackerDigi_Phase2ITChip_H
