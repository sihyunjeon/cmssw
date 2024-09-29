#ifndef DataFormats_Phase2TrackerDigi_ELink_H
#define DataFormats_Phase2TrackerDigi_ELink_H

class ELink {
private:
  int test_;

public:
  ELink(int test_num);

  void set_test(int test) { test_ = test; }

  int test() const { return test_; }
};

#endif  // DataFormats_Phase2TrackerDigi_ELink_H
