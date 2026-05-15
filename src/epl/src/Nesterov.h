#pragma once

#include <unordered_map>
#include <vector>
#include <limits>

#include "EDensity.h"
#include "Grid.h"
#include "odb/db.h"
namespace gpl {
class Instance;
}  // namespace gpl

namespace epl {
class WAwirelength;
class EDensity;
class EPlace;

class NesterovInst
{
 public:
  NesterovInst(gpl::Instance* inst) : inst_(inst)
  {
    v_x_ = u_x_ = v_x_old_ = u_x_old_ = best_x_ = inst_->cx();
    v_y_ = u_y_ = v_y_old_ = u_y_old_ = best_y_ = inst_->cy();
  };
  ~NesterovInst(){};

  gpl::Instance* gplInst() const { return inst_; };
  std::pair<float, float> const getGradient()
  {
    return std::make_pair(gradient_x_, gradient_y_);
  };
  std::pair<float, float> const getOldGradient()
  {
    return std::make_pair(gradient_x_old_, gradient_y_old_);
  };
  std::pair<float, float> getPos() const { return std::make_pair(v_x_, v_y_); };
  std::pair<float, float> getOldPos() const
  {
    return std::make_pair(v_x_old_, v_y_old_);
  };
  std::pair<float, float> getRef() const { return std::make_pair(u_x_, u_y_); };
  std::pair<float, float> getOldRef() const
  {
    return std::make_pair(u_x_old_, u_y_old_);
  };
  void setGradient(float x, float y)
  {
    gradient_x_ = x;
    gradient_y_ = y;
  };
  void setPos(float x, float y)
  {
    v_x_ = x;
    v_y_ = y;
    inst_->setCenterLocation(x, y);
  };
  void setRef(float x, float y)
  {
    u_x_ = x;
    u_y_ = y;
  };
  void updateBest()
  {
    best_x_ = v_x_;
    best_y_ = v_y_;
  };
  void restoreBest()
  {
    setPos(best_x_, best_y_);
  };
  void commitValues()
  {
    gradient_x_old_ = gradient_x_;
    gradient_y_old_ = gradient_y_;
    v_x_old_ = v_x_;
    v_y_old_ = v_y_;
    u_x_old_ = u_x_;
    u_y_old_ = u_y_;
  };

 private:
  gpl::Instance* inst_;

  // location in gpl::Instance* is int
  // but the fft calc and gradient calc is in float
  float gradient_x_ = 0, gradient_y_ = 0;
  float gradient_x_old_ = 0, gradient_y_old_ = 0;
  float v_x_ = 0, v_y_ = 0;
  float v_x_old_ = 0, v_y_old_ = 0;
  float u_x_ = 0, u_y_ = 0;
  float u_x_old_ = 0, u_y_old_ = 0;
  float best_x_ = 0, best_y_ = 0;
};

class NesterovOptimizer
{
 public:
  NesterovOptimizer(  // tconst NesterovOptimizer& npVars,
      EPlace* eplace,
      const std::shared_ptr<WAwirelength>& wa_wirelength,
      const std::vector<std::shared_ptr<EDensity>>& e_density_vec,
      utl::Logger* log);
  ~NesterovOptimizer(){};

  int stepNew();
  int step(int max_backtracking);
  float currStepLength() { return curr_step_length_; }
  std::vector<std::vector<NesterovInst>>& nesterovInsts()
  {
    return inst_ed_vec_;
  };
  std::pair<float, float> snapPosition(const odb::Rect& region,
                                       float x,
                                       float y,
                                       gpl::Instance* inst) const;

 private:
  float stepLength();
  void init();
  void updateInstances(bool commit_values);
  bool backtrack(int max_backtracking);

 private:
  EPlace* epl_;
  std::shared_ptr<WAwirelength> wa_wirelength_;
  std::vector<std::shared_ptr<EDensity>> e_density_vec_;
  utl::Logger* log_;

  std::vector<std::vector<NesterovInst>> inst_ed_vec_;
  float curr_step_length_ = std::numeric_limits<float>::infinity();
  float lst_step_length_ = 0;
  float curr_a_ = 1;
  float lst_a_ = 0;
  bool backtracking_ = false;
};

}  // namespace epl