#pragma once

#include "gpl/fft.h"
#include "gpl/placerBase.h"
#include "odb/geom.h"

namespace gpl {
class FFT;
class Instance;
class Die;
}  // namespace gpl

namespace odb {
class Rect;
}  // namespace odb

namespace epl {

class Grid : public gpl::FFT
{
 public:
  Grid(utl::Logger* log,
       int binCntX,
       int binCntY,
       float binSizeX,
       float binSizeY,
       const odb::Rect region);
  ~Grid();

  void doFFT();
  void clearMovable();
  void addFixedInst(const gpl::Instance* inst);
  void addMovableInst(const gpl::Instance* inst);
  std::pair<float, float> getElectroForce(gpl::Instance* inst) const;
  float getPotentialEnergy(gpl::Instance* inst) const;
  float getDensity(int x, int y) const
  {
    return bin_area_[x][y] / getBin(x, y).area();
  };
  float total_overflow() const;

  void setTargetDensity(float density) { target_density_ = density; };

  const odb::Rect getBin(int x, int y) const
  {
    return odb::Rect(region_.xMin() + std::round(x * bin_size_x_),
                     region_.yMin() + std::round(y * bin_size_y_),
                     region_.xMin() + std::round((x + 1) * bin_size_x_),
                     region_.yMin() + std::round((y + 1) * bin_size_y_));
  }

  int binCntX() const { return bin_cnt_X_; };
  int binCntY() const { return bin_cnt_y_; };
  float binSizeX() const { return bin_size_x_; };
  float binSizeY() const { return bin_size_y_; };
  int64_t totalInstArea() const { return total_inst_area_; };

 private:
  std::pair<int, int> getMinMaxIdxX(const gpl::Instance* inst) const;
  std::pair<int, int> getMinMaxIdxY(const gpl::Instance* inst) const;
  std::pair<float, odb::Rect> smoothScaleInst(
      const gpl::Instance* inst,
      const std::pair<int, int>& idxX,
      const std::pair<int, int>& idxY) const;

 private:
  utl::Logger* log_;
  odb::Rect region_;
  float** bin_area_ = nullptr;
  float** bin_area_filler_ = nullptr;
  int64_t** bin_area_fixed_ = nullptr;
  int64_t** bin_area_fixed_macro_ = nullptr;
  float target_density_ = 0;
  int64_t total_inst_area_ = 0;
};
}  // namespace epl