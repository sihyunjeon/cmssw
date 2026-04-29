#include <vector>
#include <utility>
#include <string>
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITQCore.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChip.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITDigiHit.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"  // FIXME move this to somewhere else?

using namespace Phase2DAQFormatSpecification;

Phase2ITChip::Phase2ITChip(int rocnum, const std::vector<Phase2ITDigiHit> hl, uint32_t detId) {
  hitList_ = hl;
  rocnum_ = rocnum;
  detId_ = detId;
}

unsigned int Phase2ITChip::size() { return hitList_.size(); }

//Returns the position (row,col) of the 4x4 QCores that contains a hit
std::pair<int, int> Phase2ITChip::getQCorePos(Phase2ITDigiHit hit) {
  int row = hit.row() / 4;
  int col = hit.col() / 4;
  return {row, col};
}

//Takes a hit and returns the 4x4 QCore that contains it
Phase2ITQCore Phase2ITChip::getQCoreFromHit(Phase2ITDigiHit pixel) {
  std::vector<int> adcs(16, 0), hits(16, 0);
  std::pair<int, int> pos = getQCorePos(pixel);

  for (const auto& hit : hitList_) {
    if (getQCorePos(hit) == pos) {
      int i = encodeQCoreIndex(hit.row(), hit.col());
      adcs[i] = hit.adc();
      hits[i] = 1;
    }
  }

  Phase2ITQCore qcore(0, pos.second, pos.first, false, false, adcs, hits);
  return qcore;
}

int Phase2ITChip::encodeQCoreIndex(int row, int col) { return ((4 * (row % 4) + (col % 4) + 8) % 16); }

std::pair<int, int> Phase2ITChip::decodeQCoreIndex(int index) {
  int adjusted = (index + 8) % 16;
  return {adjusted / 4, adjusted % 4};
}

std::pair<int, int> Phase2ITChip::getGlobalPixelCoordinate(
    int chipId, int qcoreCol, int qcoreRow, int localCol, int localRow) {
  int rowOffset = (chipId >= 2) ? kRowsPerChip : 0;
  int colOffset = (chipId % 2 == 1) ? kColsPerChip : 0;
  int globalRow = rowOffset + qcoreRow * HITMAP_COL + localRow;
  int globalCol = colOffset + qcoreCol * HITMAP_ROW + localCol;

  return {globalRow, globalCol};
}

//Removes duplicated Phase2ITQCores
std::vector<Phase2ITQCore> Phase2ITChip::remDuplicates(std::vector<Phase2ITQCore> qcores) {
  std::vector<Phase2ITQCore> list = {};

  size_t i = 0;
  while (i < qcores.size()) {
    for (size_t j = i + 1; j < qcores.size();) {
      if (qcores[j].getCol() == qcores[i].getCol() && qcores[j].getRow() == qcores[i].getRow()) {
        qcores.erase(qcores.begin() + j);
      } else {
        ++j;
      }
    }
    list.push_back(qcores[i]);
    ++i;
  }

  return list;
}

//Returns a list of the qcores with hits arranged by increasing column and then row numbers
std::vector<Phase2ITQCore> Phase2ITChip::organizeQCores(std::vector<Phase2ITQCore> qcores) {
  std::vector<Phase2ITQCore> organizedList = {};
  while (!qcores.empty()) {
    int min = 0;

    for (size_t i = 1; i < qcores.size(); i++) {
      if (qcores[i].getCol() < qcores[min].getCol()) {
        min = i;
      } else if (qcores[i].getCol() == qcores[min].getCol() && qcores[i].getRow() < qcores[min].getRow()) {
        min = i;
      }
    }

    organizedList.push_back(qcores[min]);
    qcores.erase(qcores.begin() + min);
  }

  return organizedList;
}

//Takes in an oranized list of Phase2ITQCores and sets the islast and isneighbor properties of those qcores
std::vector<Phase2ITQCore> linkQCores(std::vector<Phase2ITQCore> qcores) {
  // First set islast flags
  if (!qcores.empty()) {
    for (size_t i = 0; i < qcores.size() - 1; i++) {
      if (qcores[i].getCol() != qcores[i + 1].getCol()) {
        qcores[i].setIsLast(true);
      }
    }
    qcores[qcores.size() - 1].setIsLast(true);
  }

  // Then set isneighbour flags. Per RD53B Sec 10.4: isneighbour=1 means the
  // previous qrow address is current_qrow - 1 (i.e. consecutive qrows within
  // the same ccol), so the qrow field can be omitted on the wire. Only
  // candidates are pairs in the same ccol (previous not islast).
  for (size_t i = 1; i < qcores.size(); i++) {
    if (!qcores[i - 1].islast() && qcores[i].getRow() == qcores[i - 1].getRow() + 1) {
      qcores[i].setIsNeighbour(true);
    }
  }

  return qcores;
}

//Takes in a list of hits and organizes them into the 4x4 QCores that contains them
std::vector<Phase2ITQCore> Phase2ITChip::getOrganizedQCores() {
  std::vector<Phase2ITQCore> qcores = {};

  qcores.reserve(hitList_.size());
  for (const auto& hit : hitList_) {
    qcores.push_back(getQCoreFromHit(hit));
  }

  return (linkQCores(organizeQCores(remDuplicates(qcores))));
}

//Returns the encoding of the readout chip
std::vector<bool> Phase2ITChip::getChipCode() {
  std::vector<bool> code = {};

  if (!hitList_.empty()) {
    std::vector<Phase2ITQCore> qcores = getOrganizedQCores();
    bool isNewCol = true;

    for (auto& qcore : qcores) {
      std::vector<bool> qcoreCode = qcore.encodeQCore(isNewCol);
      code.insert(code.end(), qcoreCode.begin(), qcoreCode.end());
      isNewCol = qcore.islast();
    }
  }

  return code;
}
