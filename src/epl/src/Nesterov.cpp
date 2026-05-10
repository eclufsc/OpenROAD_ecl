#include "Nesterov.h"

#include "epl/EPlace.h"
#include "utl/Logger.h"
namespace epl {

using utl::EPL;

NesterovOptimizer::NesterovOptimizer(
    EPlace* eplace,
    const std::shared_ptr<WAwirelength>& wa_wirelength,
    const std::vector<std::shared_ptr<EDensity>>& e_density_vec,
    utl::Logger* log)
    : epl_(eplace), wa_wirelength_(std::move(wa_wirelength)), log_(log)
{
  for (auto ed : e_density_vec) {
    e_density_vec_.push_back(std::move(ed));
  }
  init();
}

void NesterovOptimizer::init()
{
  for (auto ed : e_density_vec_) {
    auto gpl_insts = ed->placeInsts();
    std::vector<NesterovInst> instances;
    instances.reserve(gpl_insts.size());
    for (auto inst : gpl_insts) {
      instances.push_back(NesterovInst(inst));
    }
    inst_ed_vec_.push_back(instances);
  }
}

std::pair<float, float> NesterovOptimizer::snapPosition(
    const odb::Rect& region,
    float x,
    float y,
    gpl::Instance* inst) const
{
  float xlo = std::max(x - inst->dx() / 2., static_cast<double>(region.xMin()));
  float xhi = xlo + inst->dx();
  if (xhi > region.xMax()) {
    xlo = region.xMax() - inst->dx();
    xhi = region.xMax();
  }

  float ylo = std::max(y - inst->dy() / 2., static_cast<double>(region.yMin()));
  float yhi = ylo + inst->dy();
  if (yhi > region.yMax()) {
    ylo = region.yMax() - inst->dy();
    yhi = region.yMax();
  }
  return std::make_pair((xlo + xhi) / 2, (ylo + yhi) / 2);
}

float NesterovOptimizer::stepLength()
{
  const float min_diff_epsilon = 1e-20f;
  const float fallback_step = e_density_vec_[0]->grid()->binSizeX() * 0.044f;
  float grad_diff = 0;
  float pos_diff = 0;
  for (auto& ed_insts : inst_ed_vec_) {
    for (auto& inst : ed_insts) {
      auto [x, y] = inst.getPos();
      auto [x_old, y_old] = inst.getOldPos();
      pos_diff += (x - x_old) * (x - x_old);
      pos_diff += (y - y_old) * (y - y_old);
      auto [gx, gy] = inst.getGradient();
      auto [gx_old, gy_old] = inst.getOldGradient();
      grad_diff += (gx - gx_old) * (gx - gx_old);
      grad_diff += (gy - gy_old) * (gy - gy_old);
    }
  }
  debugPrint(log_,
             EPL,
             "Nesterov::stepLength",
             3,
             "pos_diff: {} grad_diff: {} step: {:.4f} grid: {}",
             pos_diff,
             grad_diff,
             (pos_diff <= min_diff_epsilon || grad_diff <= min_diff_epsilon)
                 ? fallback_step
                 : std::sqrt(pos_diff / grad_diff),
             e_density_vec_[0]->grid()->binSizeX());

  if (pos_diff <= min_diff_epsilon || grad_diff <= min_diff_epsilon) {
    return fallback_step;
  }
  return std::sqrt(pos_diff / grad_diff);
}

void NesterovOptimizer::updateInstances(bool commit_values) {
  int idx = 0;
  for (auto& ed_insts : inst_ed_vec_) {
    const auto bbox = e_density_vec_[idx++]->getRegionBBox();
    for (auto& inst : ed_insts) {
      if (commit_values) {
        inst.commitValues();
      }
      // calc new ref (u)
      auto [x, y] = inst.getOldPos();
      auto [fx, fy] = inst.getOldGradient();
      auto [x_ref_new, y_ref_new] = snapPosition(bbox,
                                                 x - curr_step_length_ * fx,
                                                 y - curr_step_length_ * fy,
                                                 inst.gplInst());
      inst.setRef(x_ref_new, y_ref_new);
    
      // calc new pos (v)
      auto [x_ref_old, y_ref_old] = inst.getOldRef();
      auto [x_new, y_new] = snapPosition(
          bbox,
          x_ref_new + (lst_a_ - 1) * (x_ref_new - x_ref_old) / curr_a_,
          y_ref_new + (lst_a_ - 1) * (y_ref_new - y_ref_old) / curr_a_,
          inst.gplInst());
      inst.setPos(x_new, y_new);
    }
  }
}

int NesterovOptimizer::stepNew()
{
  if (!backtracking_) {
    lst_a_ = curr_a_;
    curr_a_ = (1.f + std::sqrt(4 * lst_a_ * lst_a_ + 1.f)) / 2;
  }

  lst_step_length_ = curr_step_length_;
  curr_step_length_ = stepLength();

  // Update the location
  updateInstances(!backtracking_);

  if (!backtracking_) {
    backtracking_ = true;
  } else {
    float epsilon = 0.95;
    backtracking_ = lst_step_length_ > (epsilon * curr_step_length_);

    debugPrint(log_,
              EPL,
              "Nesterov",
              3,
              "lst_step_length_: {} curr_step_length_: {} backtrack {}",
              lst_step_length_,
              curr_step_length_,
              backtracking_);
  }
  return backtracking_;
}

bool NesterovOptimizer::backtrack(int max_backtracking)
{
  epl_->updateGradient();
  curr_step_length_ = stepLength();

  // Update the location
  updateInstances(true);
  epl_->updateGradient();
  lst_step_length_ = curr_step_length_;
  curr_step_length_ = stepLength();

  float epsilon = 0.95;
  debugPrint(log_,
    EPL,
    "Nesterov",
    3,
    "lst_step_length_: {} curr_step_length_: {} backtrack {}",
    lst_step_length_,
    curr_step_length_,
    lst_step_length_ > epsilon * curr_step_length_);

  int backtrack = 0;
  while (lst_step_length_ > epsilon * curr_step_length_) {
    if (backtrack >= max_backtracking) {
      return false;
    }
    lst_step_length_ = curr_step_length_;
    updateInstances(false);

    epl_->updateGradient();
    curr_step_length_ = stepLength();
    backtrack++;
    debugPrint(log_,
      EPL,
      "Nesterov",
      3,
      "lst_step_length_: {} curr_step_length_: {} backtrack {}",
      lst_step_length_,
      curr_step_length_,
      lst_step_length_ > epsilon * curr_step_length_);
  }
  return true;
}

int NesterovOptimizer::step(int max_backtracking)
{
  lst_a_ = curr_a_;
  curr_a_ = (1.f + std::sqrt(4 * lst_a_ * lst_a_ + 1.f)) / 2;

  backtracking_ = backtrack(max_backtracking);
  return backtracking_;
}

}  // namespace epl