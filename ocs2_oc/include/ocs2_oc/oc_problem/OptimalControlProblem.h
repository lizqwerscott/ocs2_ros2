/******************************************************************************
Copyright (c) 2020, Farbod Farshidian. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************************************************************/

#pragma once

#include <memory>

#include <ocs2_core/Types.h>
#include <ocs2_core/constraint/StateConstraintCollection.h>
#include <ocs2_core/constraint/StateInputConstraintCollection.h>
#include <ocs2_core/cost/StateCostCollection.h>
#include <ocs2_core/cost/StateInputCostCollection.h>
#include <ocs2_core/dynamics/SystemDynamicsBase.h>

namespace ocs2 {

/** Optimal Control Problem definition */
struct OptimalControlProblem final {
  /** System dynamics pointer */
  std::unique_ptr<SystemDynamicsBase> dynamicsPtr;

  /* Constraints */
  /** Intermediate equality constraints */
  std::unique_ptr<StateInputConstraintCollection> equalityConstraintPtr;
  /** Intermediate state-only equality constraints */
  std::unique_ptr<StateConstraintCollection> stateEqualityConstraintPtr;
  /** Intermediate inequality constraints */
  std::unique_ptr<StateInputConstraintCollection> inequalityConstraintPtr;
  /** pre-jump constraints */
  std::unique_ptr<StateConstraintCollection> preJumpEqualityConstraintPtr;
  /** final constraints */
  std::unique_ptr<StateConstraintCollection> finalEqualityConstraintPtr;

  /* Soft constraints */
  /** Intermediate soft constraint penalty */
  std::unique_ptr<StateInputCostCollection> softConstraintPtr;
  /** Intermediate state-only soft constraint penalty */
  std::unique_ptr<StateCostCollection> stateSoftConstraintPtr;
  /** Pre-jump soft constraint penalty */
  std::unique_ptr<StateCostCollection> preJumpSoftConstraintPtr;
  /** Final soft constraint penalty */
  std::unique_ptr<StateCostCollection> finalSoftConstraintPtr;

  /* Cost */
  /** Intermediate cost */
  std::unique_ptr<StateInputCostCollection> costPtr;
  /** Intermediate state-only cost */
  std::unique_ptr<StateCostCollection> stateCostPtr;
  /** Pre-jump cost */
  std::unique_ptr<StateCostCollection> preJumpCostPtr;
  /** Final cost */
  std::unique_ptr<StateCostCollection> finalCostPtr;

  /** Desired trajectory reference */
  const CostDesiredTrajectories* costDesiredTrajectories = nullptr;  // TODO(mspieler) remove this

  /** The pre-computation module */
  std::unique_ptr<PreComputation> preComputationPtr;

  /** Default constructor */
  OptimalControlProblem();

  /** Copy constructor */
  OptimalControlProblem(const OptimalControlProblem& other);

  /** Move constructor */
  OptimalControlProblem(OptimalControlProblem&& other) noexcept;

  /** Copy assignment */
  OptimalControlProblem& operator=(const OptimalControlProblem& rhs);

  /** Move assignment */
  OptimalControlProblem& operator=(OptimalControlProblem&& rhs);
};

}  // namespace ocs2
