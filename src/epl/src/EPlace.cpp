#include "epl/EPlace.h"

#include <random>

#include "EDensity.h"
#include "gpl/placerBase.h"
#include "sta/StaMain.hh"

namespace sta {
// Tcl files encoded into strings.
extern const char* eplace_tcl_inits[];
}  // namespace sta

namespace epl {

using utl::EPL;

extern "C" {
extern int Epl_Init(Tcl_Interp* interp);
}

EPlace::EPlace()
{
}

EPlace::~EPlace()
{
  clear();
}

void EPlace::init(odb::dbDatabase* db, utl::Logger* logger)
{
  clear();
  db_ = db;
  log_ = logger;
}

void EPlace::clear()
{
  // clear instances
  nesterov_.reset();
  wa_wirelength_.reset();
  e_density_vec_.clear();
  pbc_.reset();
  pbVec_.clear();
  gui_.reset();
}

bool EPlace::initEPlace(float density, bool uniform_density, int pad_left, int pad_right, bool skip_io_mode)
{
  if (nesterov_) {
    log_->warn(EPL, 3, "EPlacer already initialized.");
    return true;
  }

  if (!initPlacer(pad_left, pad_right, skip_io_mode)) {
    return false;
  }

  // Init wa_wirelength_
  debugPrint(log_, EPL, "initEPlace", 3, "Init wa_wirelength_");
  int threads = 1;
  wa_wirelength_ = std::make_shared<WAwirelength>(log_, threads);

  // Init e_density
  debugPrint(log_, EPL, "initEPlace", 1, "Init e_density");
  EDensityVars edVars;
  edVars.target_density = density;
  edVars.uniform_density = uniform_density;
  for (auto pb : pbVec_) {
    e_density_vec_.push_back(
        std::make_shared<EDensity>(edVars, pb, wa_wirelength_, log_));
  }

  // Init nesterov_
  debugPrint(log_, EPL, "initEPlace", 1, "Init nesterov_");
  nesterov_ = std::make_shared<NesterovOptimizer>(
      this, wa_wirelength_, e_density_vec_, log_);

  return true;
}

bool EPlace::initPlacer(int pad_left, int pad_right, bool skip_io_mode)
{
  if (pbc_) {
    log_->warn(EPL, 2, "Placer already initialized.");
    return true;
  }
  // Init PlacerBaseCommon
  gpl::PlacerBaseVars pbVars(pad_left, pad_right, skip_io_mode, true);

  pbc_ = std::make_shared<gpl::PlacerBaseCommon>(db_, pbVars, log_);
  if (pbc_->placeInsts().size() == 0) {
    log_->warn(EPL, 15, "No placeable instances - skipping placement.");
    return false;
  }

  // Init PlacerBase
  pbVec_.push_back(std::make_shared<gpl::PlacerBase>(db_, pbc_, log_, false));
  for (auto pd : db_->getChip()->getBlock()->getPowerDomains()) {
    if (pd->getGroup()) {
      pbVec_.push_back(
          std::make_shared<gpl::PlacerBase>(db_, pbc_, log_, pd->getGroup()));
    }
  }
  return true;
}

void EPlace::set_debug(bool draw_bins,
                       bool disable_wirelength,
                       bool disable_density,
                       int pause_interval)
{
  debug_ = true;
  draw_bins_ = draw_bins;
  disable_wirelength_ = disable_wirelength;
  disable_density_ = disable_density;
  pause_interval_ = pause_interval;
}

void EPlace::place(int threads,
                   float density,
                   bool uniform_density,
                   float dhpwl_ref,
                   int iterations,
                   float initial_density_penalty_mult,
                   int pad_left,
                   int pad_right,
                   bool skip_io_mode,
                   int info_interval,
                   bool use_step_new)
{
  debugPrint(log_, EPL, "place", 1, "place: number of threads {}", threads);
  if (!initEPlace(density, uniform_density, pad_left, pad_right, skip_io_mode)) {
    return;
  }
  std::cout << "db_->getDbuPerMicron(): " << db_->getDbuPerMicron()
            << std::endl;

  int gif_key = 0;
  if (debug_ && gui::Gui::enabled()) {
    gui_ = std::make_unique<Graphics>(log_);
    gui_->debug(this,
                pbc_,
                wa_wirelength_,
                nesterov_,
                pbVec_,
                e_density_vec_,
                draw_bins_);
    for (auto ed : e_density_vec_) {
      ed->updateForce();
    }
    gui_->cellPlot(true);
    gif_key = gui_->gifStart("ePlace.gif");
  }

  debugPrint(log_,
             EPL,
             "place",
             1,
             "core size: ({} {}) ({} {})",
             pbc_->getDie().coreLx(),
             pbc_->getDie().coreLy(),
             pbc_->getDie().coreUx(),
             pbc_->getDie().coreUy());

  float curr_overflow = e_density_vec_[0]->grid()->total_overflow();
  // epl:
  // wa_wirelength_->setGamma(
  //    8.f * e_density_vec_[0]->grid()->binSizeX()
  //    * std::pow(10.f, curr_overflow * 20.f / 9.f - 11.f / 9.f));
  wa_wirelength_->setGamma(updateGamma(curr_overflow));

  density_penalty_ = 0;
  bool disable_wirelength_tmp = disable_wirelength_;
  bool disable_density_tmp = disable_density_;
  disable_wirelength_ = false;
  disable_density_ = false;
  updateGradient();
  disable_wirelength_ = disable_wirelength_tmp;
  disable_density_ = disable_density_tmp;
  // epl:
  // float density_penalty_ = total_wa_gradient_ / total_density_gradient_;
  density_penalty_ = total_wa_gradient_ / total_density_gradient_
                     * initial_density_penalty_mult;
  last_hpwl_ = wa_wirelength_->getHPWL();

  // Main placement loop
  int max_backtracking = 50;
  int iter = 0;
  float best_overflow = 100000;
  int best_iter = -1;
  log_->info(EPL,
             16,
             "Step | Overflow | HPWL (um) |  WA (um)  |    Cost    |     WA    "
             " |   Energy "
             "  |   Penalty  |  wa_grad   | density grad");
  while (iter <= iterations && curr_overflow > 0.1) {
    if ((iter % info_interval) == 0) {
      log_->info(EPL,
                 17,
                 "{:4} | {:7.2f}% | {:.3e} | {:.3e} | {:.4e} | {:.4e} | {:.4e} "
                 "| {:.4e} | "
                 "{:.4e} | {:.4e} | {:.4e}",
                 iter,
                 curr_overflow * 100,
                 static_cast<float>(wa_wirelength_->getHPWL())
                     / db_->getDbuPerMicron(),
                 wa_wirelength_->getWA() / db_->getDbuPerMicron(),
                 cost_,
                 wa_wirelength_->getWA(),
                 density_cost_,
                 density_penalty_,
                 total_wa_gradient_,
                 total_density_gradient_,
                 nesterov_->currStepLength());
    }

    // Do a nesterov step.
    if (use_step_new) {
      int curr_backtracking = 0;
      bool backtracking = true;
      while (backtracking) {
        updateGradient();
        backtracking = nesterov_->stepNew();
        curr_backtracking++;
        if (curr_backtracking >= max_backtracking) {
          std::cout << "reached max backtracking: " << curr_backtracking
                    << std::endl;
          break;
        }
      }
    } else {
      nesterov_->step(max_backtracking);
    }

    if (gui_ && gui_->enabled()) {
      gui_->cellPlot((pause_interval_ > 0)
                     && (iter % pause_interval_) == 0);  //(iter % 10) == 0);
      odb::Rect region;
      odb::Rect bbox = pbc_->db()->getChip()->getBlock()->getBBox()->getBox();
      int max_dim = std::max(bbox.dx(), bbox.dy());
      double dbu_per_pixel = static_cast<double>(max_dim) / 1000.0;
      gui_->gifAddFrame(gif_key, region, 500, dbu_per_pixel, 20);
    }

    updateDensityPenalty(wa_wirelength_->getHPWL(), last_hpwl_, dhpwl_ref);

    curr_overflow = e_density_vec_[0]->grid()->total_overflow();
    if (curr_overflow < best_overflow) {
      updateBest();
      best_overflow = curr_overflow;
      best_iter = iter;
    } else {
      if (iter - best_iter >= 100){
        restoreBest();
        log_->warn(EPL, 100, "Overflow didn't improve for 100 iterations. Restoring position of iter {}.", best_iter);
        updateGradient();
        iter = best_iter;
        iterations = iter;
        curr_overflow = e_density_vec_[0]->grid()->total_overflow();
      }
    }
    wa_wirelength_->setGamma(updateGamma(curr_overflow));
    last_hpwl_ = wa_wirelength_->getHPWL();
    iter++;
  }
  log_->info(EPL, 18, "Final values:");
  log_->info(EPL,
             19,
             "Step | Overflow | HPWL (um) |  WA (um)  |    Cost    |     WA    "
             " |   Energy "
             "  |   Penalty  |  wa_grad   | density grad");
  log_->info(
      EPL,
      20,
      "{:4} | {:7.2f}% | {:.3e} | {:.3e} | {:.4e} | {:.4e} | {:.4e} | {:.4e} | "
      "{:.4e} | {:.4e}",
      iter,
      curr_overflow * 100,
      static_cast<float>(wa_wirelength_->getHPWL()) / db_->getDbuPerMicron(),
      wa_wirelength_->getWA() / db_->getDbuPerMicron(),
      cost_,
      wa_wirelength_->getWA(),
      density_cost_,
      density_penalty_,
      total_wa_gradient_,
      total_density_gradient_);

  // update_db
  auto insts = pbc_->placeInsts();
  int n_inst = insts.size();
#pragma omp parallel for num_threads(threads)
  for (int i = 0; i < n_inst; i++) {
    auto inst = insts[i];
    inst->dbSetLocation();
    inst->dbSetPlaced();
  }

  if (gui_ && gui_->enabled()) {
    gui_->gifEnd(gif_key);
    gui_->setDebugOn(false);
    gui_->cellPlot(false);
  }
  gui_.reset();
}

void EPlace::updateBest()
{
  for (auto& ed_insts : nesterov_->nesterovInsts()) {
    for (auto& inst : ed_insts) {
      inst.updateBest();
    }
  }
}

void EPlace::restoreBest()
{
  for (auto& ed_insts : nesterov_->nesterovInsts()) {
    for (auto& inst : ed_insts) {
      inst.restoreBest();
    }
  }
}

void EPlace::updateGradient()
{
  // eDensity and wirelength gradients calc
  wa_wirelength_->update(pbc_->getNets());
  for (auto& ed : e_density_vec_) {
    ed->updateDensity();
    ed->updateForce();
  }

  // update the gradient on each instance
  density_cost_ = 0;
  total_density_gradient_ = 0;
  total_wa_gradient_ = 0;
  int idx = 0;
  for (auto& ed : e_density_vec_) {
    float filler_area = 1;
    if (use_density_field_) {
      filler_area = ed->defaultFillerArea();
    }
    for (auto& inst : nesterov_->nesterovInsts()[idx++]) {
      // Density Gradient
      float force_e_x = 0, force_e_y = 0;
      float preconditioner = 0;
      if (!disable_density_) {
        std::tie(force_e_x, force_e_y) = ed->getElectroForce(inst.gplInst());
        if (use_density_field_) {
          float area_ratio = filler_area / inst.gplInst()->getArea();
          force_e_x = force_e_x * area_ratio;
          force_e_y = force_e_y * area_ratio;
        }
        total_density_gradient_ += std::abs(force_e_x) + std::abs(force_e_y);
        preconditioner = density_penalty_ * inst.gplInst()->getArea();
      }

      // WA gradient
      float gradient_wa_x = 0, gradient_wa_y = 0;
      if (!disable_wirelength_) {
        for (auto* pin : inst.gplInst()->getPins()) {
          auto [tmp_x, tmp_y] = wa_wirelength_->getGradient(pin);
          gradient_wa_x += tmp_x;
          gradient_wa_y += tmp_y;
        }
        total_wa_gradient_ += std::abs(gradient_wa_x) + std::abs(gradient_wa_y);
        preconditioner += inst.gplInst()->getPins().size();
      }

      // Compute the total gradient
      float gradient_x = gradient_wa_x - density_penalty_ * force_e_x;
      float gradient_y = gradient_wa_y - density_penalty_ * force_e_y;
      if (use_preconditioning_) {
        gradient_x /= preconditioner;
        gradient_y /= preconditioner;
      }
      inst.setGradient(gradient_x, gradient_y);

      // calculate cost
      density_cost_ += ed->getPotentialEnergy(inst.gplInst());
    }
  }
  cost_ = wa_wirelength_->getWA() + density_penalty_ * density_cost_;
}

void EPlace::updateDensityPenalty(float curr_hpwl,
                                  float last_hpwl,
                                  float dhpwl_ref)
{
  // epl:
  // const float base_mult = 1.1f;
  // const float min_mult = 0.75f;
  const float base_mult = 1.1f;
  const float min_mult = 0.95f;
  float density_penalty_mult
      = std::pow(base_mult, 1.f - (curr_hpwl - last_hpwl) / dhpwl_ref);
  density_penalty_mult
      = std::max(std::min(density_penalty_mult, base_mult), min_mult);
  density_penalty_ *= density_penalty_mult;
}

float EPlace::updateGamma(float curr_overflow)
{
  // epl:
  // wa_wirelength_->setGamma(
  //  8.f * e_density_vec_[0]->grid()->binSizeX()
  //  * std::pow(10.f, curr_overflow * 20.f / 9.f - 11.f / 9.f));
  // if (disable_density_) {
  //  wa_wirelength_->setGamma(80.f * e_density_vec_[0]->grid()->binSizeX());
  // }
  if (disable_density_) {
    return 40.f * e_density_vec_[0]->grid()->binSizeX();
  }
  return pow(10.0, (curr_overflow - 0.1) * 20 / 9.0 - 1.0)
         * (4 * e_density_vec_[0]->grid()->binSizeX());
}

void EPlace::calcualteWaHPWL(float gamma)
{
  initPlacer(0, 0, false);
  wa_wirelength_ = std::make_shared<WAwirelength>(log_, 1);

  wa_wirelength_->update(pbc_->getNets());
  float sumgrad = 0;
  float sumgrad1 = 0;
  for (auto net : pbc_->getNets()) {
    for (auto pin : net->getPins()) {
      auto grad = wa_wirelength_->getGradient(pin);
      sumgrad += std::abs(grad.first);
      sumgrad += std::abs(grad.second);
      sumgrad1 += grad.first + grad.second;
    }
  }

  log_->info(EPL,
             21,
             "Final WA-HPWL: {:.6f} Final WA-HPWL grad: {:.6f} Final WA-HPWL "
             "gradsum: {:.6f}",
             wa_wirelength_->getWA() / db_->getDbuPerMicron(),
             sumgrad,
             sumgrad1);
  clear();
}

void EPlace::randomPlace(int threads)
{
  debugPrint(log_,
             EPL,
             "random_place",
             1,
             "random_place: number of threads {}",
             threads);
  if (!initPlacer(0, 0, false)) {
    return;
  }

  odb::dbBlock* block = db_->getChip()->getBlock();
  odb::Rect coreRect = block->getCoreArea();

  auto insts = pbc_->placeInsts();
  int n_inst = insts.size();
  std::default_random_engine generator(42);
  std::uniform_int_distribution<int> distrib_x(coreRect.xMin(),
                                               coreRect.xMax());
  std::uniform_int_distribution<int> distrib_y(coreRect.yMin(),
                                               coreRect.yMax());
  std::vector<int> pos_x(n_inst), pos_y(n_inst);
  std::generate(
      pos_x.begin(), pos_x.end(), [&]() { return distrib_x(generator); });
  std::generate(
      pos_y.begin(), pos_y.end(), [&]() { return distrib_y(generator); });

#pragma omp parallel for num_threads(threads)
  for (int i = 0; i < n_inst; i++) {
    auto inst = insts[i];
    inst->dbSetLocation(pos_x[i], pos_y[i]);
    inst->dbSetPlaced();
  }
}

}  // namespace epl
