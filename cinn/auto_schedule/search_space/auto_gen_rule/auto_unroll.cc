// Copyright (c) 2022 CINN Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cinn/auto_schedule/search_space/auto_gen_rule/auto_unroll.h"

#include <glog/logging.h>

#include <cstdlib>

#include "cinn/ir/collect_ir_nodes.h"
#include "cinn/ir/ir_printer.h"
#include "cinn/ir/ir_schedule.h"
#include "cinn/optim/ir_copy.h"

namespace cinn {
namespace auto_schedule {

static std::vector<int> auto_unroll_options = {0, 8, 32, 128};

bool AutoUnroll::MeetCondition(const ir::ScheduleBlock* schedule_block) {
  // whether any block has reduce iter
  auto has_reduce_iter = [](const Expr* x) {
    auto* block_realize = x->As<ir::ScheduleBlockRealize>();
    if (block_realize) {
      auto* schedule_block = block_realize->schedule_block.As<ir::ScheduleBlock>();
      CHECK(schedule_block) << "schedule_block field is not a ScheduleBlock";
      for (auto&& var : schedule_block->iter_vars) {
        if (var->is_reduce_axis) {
          VLOG(6) << "find ScheduleBlockRealize:" << *x << " has reduce_axis:" << var;
          return true;
        }
      }
    }
    return false;
  };
  // whether has any for-loop with non-serial type
  auto has_nonserial_loop = [](const Expr* x) {
    if (x->As<ir::For>() && x->As<ir::For>()->for_type() != ir::ForType::Serial) {
      VLOG(6) << "find non-serial loop:" << *x;
      return true;
    }
    return false;
  };

  auto find_target_exprs = ir::CollectIRNodesWithoutTensor(
      schedule_block->body,
      [&has_reduce_iter, &has_nonserial_loop](const Expr* x) { return has_reduce_iter(x) || has_nonserial_loop(x); });

  return !find_target_exprs.empty();
}

RuleApplyType AutoUnroll::Init(ir::IRSchedule* ir_schedule) {
  ir_schedule_        = ir_schedule;
  auto block_realizes = ir_schedule_->GetAllBlocks();

  // A schedule block can perform `auto_unroll` rule should meet two conditions:
  // (1) it is a root block
  // (2) MeetCondition returns true with it
  applicable_schedule_blocks_.clear();
  std::set<Expr> deduplicate_results;
  for (size_t i = 0; i < block_realizes.size(); ++i) {
    // find root block
    Expr root_block     = ir_schedule_->GetRootBlock(block_realizes[i]);
    auto* block_realize = root_block.As<ir::ScheduleBlockRealize>();
    CHECK(block_realize) << "stmt is not a ScheduleBlockRealize:" << root_block;
    auto* schedule_block = block_realize->schedule_block.As<ir::ScheduleBlock>();
    CHECK(schedule_block) << "schedule_block field is not a ScheduleBlock:" << Expr(block_realize);
    if (MeetCondition(schedule_block)) {
      deduplicate_results.emplace(root_block);
    }
  }
  applicable_schedule_blocks_ = {deduplicate_results.begin(), deduplicate_results.end()};
  num_applicable_             = applicable_schedule_blocks_.size();
  VLOG(6) << "Collect applicable_schedule_blocks_:" << num_applicable_;

  return num_applicable_ > 0 ? RuleApplyType::kApplyAndSkipThisRule : RuleApplyType::kCannotApply;
}

void AutoUnroll::Apply(int index) {
  CHECK_LT(index, applicable_schedule_blocks_.size()) << "invalid apply index:" << index;
  auto applied_block = applicable_schedule_blocks_.at(index);
  int max_step       = auto_unroll_options[std::rand() % auto_unroll_options.size()];
  ir_schedule_->Annotate(applied_block, ir::attr::auto_unroll_max_step, max_step);
  return;
}

}  // namespace auto_schedule
}  // namespace cinn
