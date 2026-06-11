#ifndef DataFormats_Phase2TrackerDigi_ChipModuleMap_h
#define DataFormats_Phase2TrackerDigi_ChipModuleMap_h

/**
 * @file ChipModuleMap.h
 * @brief Single source of truth for the chip <-> physical-quadrant convention.
 *
 * A module's pixels are split into chips on a grid of (row-half, col-half).
 * With the IT axis convention  x = row,  y = column  (x grows with rows,
 * y grows with columns), each chip occupies a quadrant (x_sign, y_sign) with
 * signs in {-1, 0, +1}  (0 = no split along that axis, i.e. 1- or 2-chip
 * modules).
 *
 * itchip::CHIP_QUADRANT is keyed by Module_SubType and lists, for chip 0..N-1,
 * the (x_sign, y_sign) that chip occupies -- read top-to-bottom / left-to-right
 * it is exactly the HDI module-type / CROC-numbering table.
 *
 * The packing (PixelToBitStreamProducer) and unpacking (Phase2ITChip) paths
 * both include it so the chip-index convention cannot drift between them:
 *   pack   uses chipIndex()  : (subtype, x_sign, y_sign) -> chip index
 *   unpack uses quadrantOf() : (subtype, chip index)     -> (x_sign, y_sign)
 * which are exact inverses.
 */

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace itchip {

  /// Chip index -> (x_sign, y_sign) quadrant, keyed by Module_SubType (1..12).
  inline const std::map<int, std::vector<std::pair<int, int>>> CHIP_QUADRANT = {
      {1, {{0, 0}}},                                   // TBPX L1   : 1 chip (centre)
      {2, {{0, +1}, {0, -1}}},                         // TBPX L2   : 2 chips
      {3, {{+1, +1}, {+1, -1}, {-1, -1}, {-1, +1}}},   // TBPX L3
      {4, {{+1, +1}, {+1, -1}, {-1, -1}, {-1, +1}}},   // TBPX L4
      {5, {{0, -1}, {0, +1}}},                         // TFPX R1/R2: 2 chips
      {6, {{+1, -1}, {+1, +1}, {-1, -1}, {-1, +1}}},   // TFPX R3   (Y-flipped)
      {7, {{+1, -1}, {+1, +1}, {-1, -1}, {-1, +1}}},   // TFPX R4   (Y-flipped)
      {8, {{+1, +1}, {+1, -1}, {-1, -1}, {-1, +1}}},   // TEPX R1
      {9, {{+1, +1}, {+1, -1}, {-1, -1}, {-1, +1}}},   // TEPX R2
      {10, {{+1, +1}, {+1, -1}, {-1, -1}, {-1, +1}}},  // TEPX R2/R3
      {11, {{+1, +1}, {+1, -1}, {-1, -1}, {-1, +1}}},  // TEPX R4
      {12, {{+1, +1}, {+1, -1}, {-1, -1}, {-1, +1}}},  // TEPX R5
  };

  /**
   * @brief Number of chips on a module of the given Module_SubType.
   * @param subtype HDI Module_SubType (1..12).
   * @return chip count (= length of the CHIP_QUADRANT row for that subtype).
   * @throws std::runtime_error if @p subtype is not in the table.
   */
  inline int nChips(int subtype) {
    auto row = CHIP_QUADRANT.find(subtype);
    if (row == CHIP_QUADRANT.end())
      throw std::runtime_error("ChipModuleMap: unknown Module_SubType " + std::to_string(subtype));
    return static_cast<int>(row->second.size());
  }

  /**
   * @brief Map a grid cell (row-half, col-half) to its signed (x, y) quadrant.
   *
   * Single source of the x=row, y=column axis convention. A module with a single
   * chip along an axis yields sign 0 on that axis.
   * @param ichip_row  chip row index (0-based).
   * @param ichip_col  chip column index (0-based).
   * @param nchip_rows number of chip rows on the module (1 or 2).
   * @param nchip_cols number of chip columns on the module (1 or 2).
   * @return (x_sign, y_sign), each in {-1, 0, +1}.
   */
  inline std::pair<int, int> gridToQuadrant(int ichip_row, int ichip_col, int nchip_rows, int nchip_cols) {
    int x = (nchip_rows == 1) ? 0 : (ichip_row ? +1 : -1);
    int y = (nchip_cols == 1) ? 0 : (ichip_col ? +1 : -1);
    return {x, y};
  }

  /**
   * @brief Quadrant -> chip index for a given Module_SubType (packing direction).
   *
   * Throws (rather than silently mis-wiring) if the subtype or the quadrant is
   * not present in the table.
   * @param subtype HDI Module_SubType.
   * @param x_sign  row-axis sign (-1, 0, +1).
   * @param y_sign  column-axis sign (-1, 0, +1).
   * @return chip index (0-based) occupying that quadrant.
   * @throws std::runtime_error on unknown subtype or quadrant.
   */
  inline int chipIndex(int subtype, int x_sign, int y_sign) {
    auto row = CHIP_QUADRANT.find(subtype);
    if (row == CHIP_QUADRANT.end())
      throw std::runtime_error("ChipModuleMap: unknown Module_SubType " + std::to_string(subtype));
    auto it = std::find(row->second.begin(), row->second.end(), std::make_pair(x_sign, y_sign));
    if (it == row->second.end())
      throw std::runtime_error("ChipModuleMap: quadrant (" + std::to_string(x_sign) + "," + std::to_string(y_sign) +
                               ") not in subtype " + std::to_string(subtype));
    return static_cast<int>(std::distance(row->second.begin(), it));
  }

  /**
   * @brief Chip index -> quadrant for a given Module_SubType (unpacking direction).
   *
   * Exact inverse of chipIndex(): recovers the (x_sign, y_sign) a chip occupies
   * so the decoder can rebuild the module-coordinate offsets.
   * @param subtype HDI Module_SubType.
   * @param chip    chip index (0-based).
   * @return (x_sign, y_sign), each in {-1, 0, +1}.
   * @throws std::runtime_error on unknown subtype or out-of-range chip index.
   */
  inline std::pair<int, int> quadrantOf(int subtype, int chip) {
    auto row = CHIP_QUADRANT.find(subtype);
    if (row == CHIP_QUADRANT.end())
      throw std::runtime_error("ChipModuleMap: unknown Module_SubType " + std::to_string(subtype));
    if (chip < 0 || chip >= static_cast<int>(row->second.size()))
      throw std::runtime_error("ChipModuleMap: chip index " + std::to_string(chip) + " out of range for subtype " +
                               std::to_string(subtype));
    return row->second[static_cast<size_t>(chip)];
  }

}  // namespace itchip

#endif  // DataFormats_Phase2TrackerDigi_ChipModuleMap_h
