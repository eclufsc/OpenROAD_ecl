#include "Grid.h"

#include "gpl/placerBase.h"
#include "odb/db.h"
#include "odb/geom.h"

namespace gpl {
class FFT;
}  // namespace gpl

namespace odb {
class Rect;
}  // namespace odb

namespace epl {

Grid::Grid(utl::Logger* log,
           int binCntX,
           int binCntY,
           float binSizeX,
           float binSizeY,
           const odb::Rect region)
    : gpl::FFT(binCntX, binCntY, binSizeX, binSizeY), log_(log), region_(region)
{
  bin_area_ = new float*[bin_cnt_X_];
  bin_area_filler_ = new float*[bin_cnt_X_];
  bin_area_fixed_ = new int64_t*[bin_cnt_X_];
  bin_area_fixed_macro_ = new int64_t*[bin_cnt_X_];

  for (int x = 0; x < bin_cnt_X_; x++) {
    bin_area_[x] = new float[bin_cnt_y_];
    bin_area_filler_[x] = new float[bin_cnt_y_];
    bin_area_fixed_[x] = new int64_t[bin_cnt_y_];
    bin_area_fixed_macro_[x] = new int64_t[bin_cnt_y_];

    for (int y = 0; y < bin_cnt_y_; y++) {
      bin_area_[x][y] = 0.0f;
      bin_area_filler_[x][y] = 0.0f;
      bin_area_fixed_[x][y] = 0;
      bin_area_fixed_macro_[x][y] = 0;
    }
  }
}

Grid::~Grid()
{
  for (int x = 0; x < bin_cnt_X_; x++) {
    delete[] bin_area_[x];
    delete[] bin_area_filler_[x];
    delete[] bin_area_fixed_[x];
    delete[] bin_area_fixed_macro_[x];
  }
  delete[] bin_area_;
  delete[] bin_area_filler_;
  delete[] bin_area_fixed_;
  delete[] bin_area_fixed_macro_;
}

void Grid::doFFT()
{
  for (int x = 0; x < bin_cnt_X_; x++) {
    for (int y = 0; y < bin_cnt_y_; y++) {
      bin_density_[x][y] = bin_area_[x][y] / getBin(x, y).area();
    }
  }
  gpl::FFT::doFFT();
}

void Grid::clearMovable()
{
  for (int x = 0; x < bin_cnt_X_; x++) {
    for (int y = 0; y < bin_cnt_y_; y++) {
      bin_area_[x][y] = (bin_area_fixed_[x][y]
                         + bin_area_fixed_macro_[x][y]) * target_density_;
      bin_area_filler_[x][y] = 0;
    }
  }
  total_inst_area_ = 0;
}

void Grid::addFixedInst(const gpl::Instance* inst)
{
  std::pair<int, int> idxX = getMinMaxIdxX(inst);
  std::pair<int, int> idxY = getMinMaxIdxY(inst);
  odb::Rect inst_rect{inst->lx(), inst->ly(), inst->ux(), inst->uy()};
  int64_t** bin_area = bin_area_fixed_;
  if (inst->isMacro()) {
    bin_area = bin_area_fixed_macro_;
  }
  for (int x = idxX.first; x < idxX.second; x++) {
    for (int y = idxY.first; y < idxY.second; y++) {
      bin_area[x][y] += inst_rect.intersect(getBin(x, y)).area();
    }
  }
}

void Grid::addMovableInst(const gpl::Instance* inst)
{
  std::pair<int, int> idxX = getMinMaxIdxX(inst);
  std::pair<int, int> idxY = getMinMaxIdxY(inst);
  const auto [scaling, inst_rect] = smoothScaleInst(inst, idxX, idxY);
  float filler = 1.f;
  if (inst->isInstance()) {
    filler = 0.f;
    total_inst_area_ += static_cast<int64_t>(scaling * inst_rect.area());
    /* if (std::abs((scaling * inst_rect.area()) - inst->getArea()) > 10) {
      std::cout << inst->dbInst()->getName() << " diff "
                << scaling * inst_rect.area() - inst->getArea()
                << " scaling * inst_rect.area()): "
                << scaling * inst_rect.area()
                << " inst->getArea(): " << inst->getArea() << std::endl;
    } */
  }

  for (int x = idxX.first; x < idxX.second; x++) {
    for (int y = idxY.first; y < idxY.second; y++) {
      float area = inst_rect.intersect(getBin(x, y)).area() * scaling;
      bin_area_[x][y] += area;
      bin_area_filler_[x][y] += area * filler;
    }
  }
}

std::pair<float, float> Grid::getElectroForce(gpl::Instance* inst) const
{
  std::pair<int, int> idxX = getMinMaxIdxX(inst);
  std::pair<int, int> idxY = getMinMaxIdxY(inst);
  const auto [scaling, inst_rect] = smoothScaleInst(inst, idxX, idxY);

  float force_x = 0, force_y = 0;
  for (int x = idxX.first; x < idxX.second; x++) {
    for (int y = idxY.first; y < idxY.second; y++) {
      float intersect_ratio = float(inst_rect.intersect(getBin(x, y)).area())
                              / getBin(x, y).area();
      force_x += electro_field_x_[x][y] * intersect_ratio;
      force_y += electro_field_y_[x][y] * intersect_ratio;
    }
  }
  return std::make_pair(force_x * scaling, force_y * scaling);
}

float Grid::getPotentialEnergy(gpl::Instance* inst) const
{
  std::pair<int, int> idxX = getMinMaxIdxX(inst);
  std::pair<int, int> idxY = getMinMaxIdxY(inst);
  const auto [scaling, inst_rect] = smoothScaleInst(inst, idxX, idxY);

  float energy = 0;
  for (int x = idxX.first; x < idxX.second; x++) {
    for (int y = idxY.first; y < idxY.second; y++) {
      float intersect_ratio = float(inst_rect.intersect(getBin(x, y)).area())
                              / getBin(x, y).area();
      energy += electro_phi_[x][y] * intersect_ratio;
    }
  }
  return energy * scaling;
}

std::pair<int, int> Grid::getMinMaxIdxX(const gpl::Instance* inst) const
{
  int lowerIdx = (inst->lx() - region_.xMin()) / bin_size_x_;
  int upperIdx = std::ceil((inst->ux() - region_.xMin()) / bin_size_x_);

  return std::make_pair(std::max(lowerIdx, 0), std::min(upperIdx, bin_cnt_X_));
}

std::pair<int, int> Grid::getMinMaxIdxY(const gpl::Instance* inst) const
{
  int lowerIdx = (inst->ly() - region_.yMin()) / bin_size_y_;
  int upperIdx = std::ceil((inst->uy() - region_.yMin()) / bin_size_y_);

  return std::make_pair(std::max(lowerIdx, 0), std::min(upperIdx, bin_cnt_y_));
}

std::pair<float, odb::Rect> Grid::smoothScaleInst(
    const gpl::Instance* inst,
    const std::pair<int, int>& idxX,
    const std::pair<int, int>& idxY) const
{
  // Local smoothness over discrete grids

  // Makes the instance at least the size of one bin
  odb::Rect inst_rect{inst->lx(), inst->ly(), inst->ux(), inst->uy()};
  if (inst_rect.dx() < bin_size_x_) {
    int new_xlo = inst_rect.xCenter() - static_cast<int>(bin_size_x_ / 2);
    new_xlo = std::max(new_xlo, region_.xMin());

    int new_xhi = new_xlo + bin_size_x_;
    if (new_xhi > region_.xMax()) {
      new_xlo = region_.xMax() - bin_size_x_;
      new_xhi = region_.xMax();
    }
    inst_rect.set_xlo(new_xlo);
    inst_rect.set_xhi(new_xhi);
  }
  if (inst_rect.dy() < bin_size_y_) {
    int new_ylo = inst_rect.yCenter() - static_cast<int>(bin_size_y_ / 2);
    new_ylo = std::max(new_ylo, region_.yMin());

    int new_yhi = inst_rect.yMin() + bin_size_y_;
    if (new_yhi > region_.yMax()) {
      new_ylo = region_.yMax() - bin_size_y_;
      new_yhi = region_.yMax();
    }
    inst_rect.set_ylo(new_ylo);
    inst_rect.set_yhi(new_yhi);
  }

  // Calculate the ratio between the original size and the inflated size
  double scaling = inst->getArea() / static_cast<double>(inst_rect.area());
  if (inst->isMacro()) {
    scaling *= target_density_;
  }

  return std::make_pair(scaling, inst_rect);
}

float Grid::total_overflow() const
{
  float total_overflow = 0;
  for (int x = 0; x < bin_cnt_X_; x++) {
    for (int y = 0; y < bin_cnt_y_; y++) {
      float target_area = getBin(x, y).area() * target_density_;
      float inst_overflow = std::max(
          (bin_area_[x][y] - bin_area_filler_[x][y]) - target_area, 0.f);
      total_overflow += inst_overflow;
      float fixed_overflow
          = std::max(((bin_area_fixed_[x][y]
                      + bin_area_fixed_macro_[x][y]) * target_density_)
                         - target_area,
                     0.f);
      total_overflow -= fixed_overflow;
    }
  }
  return total_overflow / total_inst_area_;
}

}  // namespace epl