#ifndef READOUTCHIP_H
#define READOUTCHIP_H
#include <vector>
#include <utility>
#include <string>
#include "DataFormats/Phase2TrackerDigi/QCore.h"
#include "DataFormats/Phase2TrackerDigi/Hit.h"

class ReadoutChip {
    std::vector<Hit> hitList;
    int rocnum_;

    public:
        ReadoutChip(int rocnum, std::vector<Hit> hl);

        unsigned int size();
        int rocnum() const { return rocnum_; }

        std::vector<QCore> get_organized_QCores();
        std::vector<bool> get_chip_code();

    private:
        std::pair<int,int> get_QCore_pos(Hit hit);

        QCore get_QCore_from_hit(Hit pixel);
        std::vector<QCore> rem_duplicates(std::vector<QCore> qcores);
        std::vector<QCore> organize_QCores(std::vector<QCore> qcores);
};

#endif // READOUTCHIP_H
