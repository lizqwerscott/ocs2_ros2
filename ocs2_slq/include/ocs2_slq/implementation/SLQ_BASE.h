/*
 * Implementation of SLQ_BASE.h
 *
 *  Created on: October 7, 2016
 *      Author: farbod
 */


namespace ocs2
{

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::SLQ_BASE(
		const controlled_system_base_t* systemDynamicsPtr,
		const derivatives_base_t* systemDerivativesPtr,
		const constraint_base_t* systemConstraintsPtr,
		const cost_function_base_t* costFunctionPtr,
		const operating_trajectories_base_t* operatingTrajectoriesPtr,
		const SLQ_Settings& settings /*= SLQ_Settings()*/,
		const LOGIC_RULES_T* logicRulesPtr /*= nullptr*/,
		const cost_function_base_t* heuristicsFunctionPtr /* = nullptr*/)

	: settings_(settings)
	, costDesiredTrajectories_()
	, costDesiredTrajectoriesBuffer_()
	, costDesiredTrajectoriesUpdated_(false)
	, rewindCounter_(0)
{
	if (logicRulesPtr != nullptr)
		logicRulesMachinePtr_ = logic_rules_machine_ptr_t( new logic_rules_machine_t(*logicRulesPtr) );
	else
		logicRulesMachinePtr_ = logic_rules_machine_ptr_t( new logic_rules_machine_t(LOGIC_RULES_T()) );

	// Dynamics, Constraints, derivatives, and cost
	systemDynamicsPtrStock_.clear();
	systemDynamicsPtrStock_.reserve(settings_.nThreads_);
	systemDerivativesPtrStock_.clear();
	systemDerivativesPtrStock_.reserve(settings_.nThreads_);
	systemConstraintsPtrStock_.clear();
	systemConstraintsPtrStock_.reserve(settings_.nThreads_);
	costFunctionsPtrStock_.clear();
	costFunctionsPtrStock_.reserve(settings_.nThreads_);
	heuristicsFunctionsPtrStock_.clear();
	heuristicsFunctionsPtrStock_.reserve(settings_.nThreads_);
	systemEventHandlersPtrStock_.clear();
	systemEventHandlersPtrStock_.reserve(settings_.nThreads_);
	dynamicsIntegratorsPtrStock_.clear();
	dynamicsIntegratorsPtrStock_.reserve(settings_.nThreads_);

	// initialize all subsystems, etc.
	for (size_t i=0; i<settings_.nThreads_; i++) {

		// initialize dynamics
		systemDynamicsPtrStock_.emplace_back( systemDynamicsPtr->clone() );

		// initialize linearized systems
		systemDerivativesPtrStock_.emplace_back( systemDerivativesPtr->clone() );

		// initialize constraints
		systemConstraintsPtrStock_.emplace_back( systemConstraintsPtr->clone() );

		// initialize cost functions
		costFunctionsPtrStock_.emplace_back( costFunctionPtr->clone() );

		// initialize operating trajectories
		operatingTrajectoriesPtrStock_.emplace_back( operatingTrajectoriesPtr->clone() );

		// initialize heuristics functions
		if (heuristicsFunctionPtr != nullptr)
			heuristicsFunctionsPtrStock_.emplace_back( heuristicsFunctionPtr->clone() );
		else // use the cost function if no heuristics function is defined
			heuristicsFunctionsPtrStock_.emplace_back( costFunctionPtr->clone() );

		// initialize events
		typedef Eigen::aligned_allocator<event_handler_t> event_handler_alloc_t;
		systemEventHandlersPtrStock_.push_back(std::move( std::allocate_shared<event_handler_t, event_handler_alloc_t>(event_handler_alloc_t()) ));

		// initialize integrators
		typedef ODE45<STATE_DIM> ode_t;
		typedef Eigen::aligned_allocator<ode_t> ode_alloc_t;
		dynamicsIntegratorsPtrStock_.push_back(std::move( std::allocate_shared<ode_t, ode_alloc_t>(
				ode_alloc_t(), systemDynamicsPtrStock_.back(), systemEventHandlersPtrStock_.back()) ));

	} // end of i loop


	// State triggered
	eventsPtrStock_.clear();
	eventsPtrStock_.reserve(settings_.nThreads_);
	integratorsPtrStock_.clear();
	integratorsPtrStock_.reserve(settings_.nThreads_);
	// initialize all subsystems, etc.
	for (size_t i=0; i<settings_.nThreads_; i++) {

		// initialize events
		typedef Eigen::aligned_allocator<state_triggered_event_handler_t> event_handler_alloc_t;
		eventsPtrStock_.push_back(std::move( std::allocate_shared<state_triggered_event_handler_t, event_handler_alloc_t>(
				event_handler_alloc_t()) ));

		// initialize integrators
		typedef ODE45<STATE_DIM> ode_t;
		typedef Eigen::aligned_allocator<ode_t> ode_alloc_t;
		integratorsPtrStock_.push_back(std::move( std::allocate_shared<ode_t, ode_alloc_t>(
				ode_alloc_t(), systemDynamicsPtrStock_[i], eventsPtrStock_[i]) ));

	} // end of i loop


	// for controller design
	nominalStateFunc_.resize(settings.nThreads_);
	nominalInputFunc_.resize(settings.nThreads_);
	BmFunc_.resize(settings.nThreads_);
	PmFunc_.resize(settings.nThreads_);
	RmInverseFunc_.resize(settings.nThreads_);
	RvFunc_.resize(settings.nThreads_);
	EvProjectedFunc_.resize(settings.nThreads_);
	CmProjectedFunc_.resize(settings.nThreads_);
	DmProjectedFunc_.resize(settings.nThreads_);

	// Riccati Solver
	SmFuncs_.resize(settings_.nThreads_);
	riccatiEquationsPtrStock_.clear();
	riccatiEquationsPtrStock_.reserve(settings_.nThreads_);
	errorEquationPtrStock_.clear();
	errorEquationPtrStock_.reserve(settings_.nThreads_);
	riccatiIntegratorPtrStock_.clear();
	riccatiIntegratorPtrStock_.reserve(settings_.nThreads_);
	errorIntegratorPtrStock_.clear();
	errorIntegratorPtrStock_.reserve(settings_.nThreads_);

	slqRiccatiEquationsPtrStock_.clear();
	slqRiccatiEquationsPtrStock_.reserve(settings_.nThreads_);
	slqRiccatiIntegratorPtrStock_.clear();
	slqRiccatiIntegratorPtrStock_.reserve(settings_.nThreads_);

	for (size_t i=0; i<settings_.nThreads_; i++)  {

		typedef Eigen::aligned_allocator<riccati_equations_t> riccati_equations_alloc_t;
		riccatiEquationsPtrStock_.push_back( std::move(
				std::allocate_shared<riccati_equations_t, riccati_equations_alloc_t>(riccati_equations_alloc_t(), settings_.useMakePSD_) ) );

		typedef Eigen::aligned_allocator<error_equation_t> error_equation_alloc_t;
		errorEquationPtrStock_.push_back( std::move(
				std::allocate_shared<error_equation_t, error_equation_alloc_t>(error_equation_alloc_t(), settings_.useMakePSD_) ) );

		typedef Eigen::aligned_allocator<slq_riccati_equations_t> slq_riccati_equations_alloc_t;
		slqRiccatiEquationsPtrStock_.push_back( std::move(
				std::allocate_shared<slq_riccati_equations_t, slq_riccati_equations_alloc_t>(slq_riccati_equations_alloc_t(), settings_.useMakePSD_) ) );

		switch(settings_.RiccatiIntegratorType_) {

		case DIMENSIONS::RICCATI_INTEGRATOR_TYPE::ODE45 : {
			riccatiIntegratorPtrStock_.emplace_back( new ODE45<riccati_equations_t::S_DIM_>(riccatiEquationsPtrStock_.back()) );
			errorIntegratorPtrStock_.emplace_back( new ODE45<STATE_DIM>(errorEquationPtrStock_.back()) );
			slqRiccatiIntegratorPtrStock_.emplace_back( new ODE45<slq_riccati_equations_t::S_DIM_>(slqRiccatiEquationsPtrStock_.back()) );
			break;
		}
		/*note: this case is not yet working. It would most likely work if we had an adaptive time adams-bashforth integrator */
		case DIMENSIONS::RICCATI_INTEGRATOR_TYPE::ADAMS_BASHFORTH : {
			throw std::runtime_error("This ADAMS_BASHFORTH is not implemented for Riccati Integrator.");
			break;
		}
		case DIMENSIONS::RICCATI_INTEGRATOR_TYPE::BULIRSCH_STOER : {
			riccatiIntegratorPtrStock_.emplace_back( new IntegratorBulirschStoer<riccati_equations_t::S_DIM_>(riccatiEquationsPtrStock_.back()) );
			errorIntegratorPtrStock_.emplace_back( new IntegratorBulirschStoer<STATE_DIM>(errorEquationPtrStock_.back()) );
			slqRiccatiIntegratorPtrStock_.emplace_back( new IntegratorBulirschStoer<slq_riccati_equations_t::S_DIM_>(slqRiccatiEquationsPtrStock_.back()) );
			break;
		}
		default:
			throw (std::runtime_error("Riccati equation integrator type specified wrongly."));
		}

	}  // end of i loop

	hamiltonianEquationPtrStock_.clear();
	hamiltonianEquationPtrStock_.reserve(settings_.nThreads_);
	hamiltonianIntegratorPtrStock_.clear();
	hamiltonianIntegratorPtrStock_.reserve(settings_.nThreads_);

	hamiltonianIncrementEquationPtrStock_.clear();
	hamiltonianIncrementEquationPtrStock_.reserve(settings_.nThreads_);
	hamiltonianIncrementIntegratorPtrStock_.clear();
	hamiltonianIncrementIntegratorPtrStock_.reserve(settings_.nThreads_);

	for (size_t i=0; i<settings_.nThreads_; i++)  {
		hamiltonianEquationPtrStock_.emplace_back(
				new hamiltonian_equation_t());
		hamiltonianIntegratorPtrStock_.emplace_back(
				new ODE45<hamiltonian_equation_t::LTI_DIM_>(hamiltonianEquationPtrStock_[i]));

		hamiltonianIncrementEquationPtrStock_.emplace_back(
				new hamiltonian_increment_equation_t());
		hamiltonianIncrementIntegratorPtrStock_.emplace_back(
				new ODE45<hamiltonian_increment_equation_t::LTI_DIM_>(hamiltonianIncrementEquationPtrStock_[i]));
	}  // end of i loop

}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::~SLQ_BASE()  {

#ifdef BENCHMARK
	auto BENCHMARK_total = BENCHMARK_tAvgFP_ + BENCHMARK_tAvgBP_ + BENCHMARK_tAvgLQ_;
	if (BENCHMARK_total>0) {
		std::cerr << std::endl << "#####################################################" << std::endl;
		std::cerr << "Benchmarking over " << BENCHMARK_nIterationsBP_ << " samples." << std::endl;
		std::cerr << "Average time for Forward Pass:      " << BENCHMARK_tAvgFP_ << " [ms] \t(" <<
				BENCHMARK_tAvgFP_/BENCHMARK_total*100 << "%)" << std::endl;
		std::cerr << "Average time for Backward Pass:     " << BENCHMARK_tAvgBP_ << " [ms] \t(" <<
				BENCHMARK_tAvgBP_/BENCHMARK_total*100 << "%)" << std::endl;
		std::cerr << "Average time for LQ Approximation:  " << BENCHMARK_tAvgLQ_ << " [ms] \t(" <<
				BENCHMARK_tAvgLQ_/BENCHMARK_total*100 << "%)" << std::endl;
	}
#endif
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::state_vector_t
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::rolloutStateTriggeredWorker(
			size_t workerIndex,
			const size_t& partitionIndex,
			const scalar_t& initTime,
			const state_vector_t& initState,
			const scalar_t& finalTime,
			const controller_t& controller,
			scalar_array_t& timeTrajectory,
			size_array_t& eventsPastTheEndIndeces,
			state_vector_array_t& stateTrajectory,
			input_vector_array_t& inputTrajectory,
			scalar_array_t& eventTimes,
			size_array_t& subsystemID,
			scalar_array_t& guardSurfacesValues,
			hybrid_logic_rules_machine_t& hybridLlogicRulesMachine)  {

	if (initTime > finalTime)
		throw std::runtime_error("Initial time should be less-equal to final time.");
	if (eventTimes.empty()==false && guardSurfacesValues.empty()==true)
		throw std::runtime_error("Since the event times array is not empty, "
				"the last update of the guard functions value should be provided.");

	// max number of steps for integration
	const size_t maxNumSteps = settings_.maxNumStepsPerSecond_ * std::max(1.0, finalTime-initTime);

	// clearing the output trajectories
	timeTrajectory.clear();
	timeTrajectory.reserve(maxNumSteps+1);
	stateTrajectory.clear();
	stateTrajectory.reserve(maxNumSteps+1);
	inputTrajectory.clear();
	inputTrajectory.reserve(maxNumSteps+1);
	eventsPastTheEndIndeces.clear();

	// initialize the model and set controller
	if (controller.empty()==false) {
		// init Hybrid Logic Machine
		hybridLlogicRulesMachine.initLogicMachine(partitionIndex);
//		std::cerr << std::endl << "+++++++++++++ partitionIndex: " << partitionIndex;
//		hybridLlogicRulesMachine.display();
		// set controller
		systemDynamicsPtrStock_[workerIndex]->setController(controller);
		// reset function calls counter
		systemDynamicsPtrStock_[workerIndex]->resetNumFunctionCalls();
		// initialize subsystem
		systemDynamicsPtrStock_[workerIndex]->initializeModel(hybridLlogicRulesMachine, partitionIndex, "SLQ");
		// Set event times control parameters
		if (eventTimes.empty()==true)
			eventsPtrStock_[workerIndex]->setEventTimesGuard(settings_.minEventTimeDifference_);
		else
			eventsPtrStock_[workerIndex]->setEventTimesGuard(settings_.minEventTimeDifference_, eventTimes.back(), guardSurfacesValues);
	}

	// initial values of the guard surfaces
	if (subsystemID.empty()==true) {
		size_t activeSubsystem = 0;
		scalar_array_t initGuardSurfacesValue;
		systemDynamicsPtrStock_[workerIndex]->computeGuardSurfaces(initTime, initState, initGuardSurfacesValue);
		for (size_t i=0; i<initGuardSurfacesValue.size(); i++)
			if (initGuardSurfacesValue[i]<0)
				activeSubsystem = i;

		subsystemID.push_back(activeSubsystem);
	}

	scalar_t t0 = initTime;
	state_vector_t x0 = initState;

	while (t0 < finalTime-ocs2::OCS2NumericTraits<scalar_t>::week_epsilon()) {

		try {
			// integrate controlled system
			integratorsPtrStock_[workerIndex]->integrate(x0, t0, finalTime,
					stateTrajectory, timeTrajectory,
					settings_.minTimeStep_, settings_.absTolODE_, settings_.relTolODE_, maxNumSteps, true);

		} catch (const size_t& eventID) {

			eventsPastTheEndIndeces.push_back( timeTrajectory.size() );
			systemDynamicsPtrStock_[workerIndex]->computeJumpMap(timeTrajectory.back(), stateTrajectory.back(), x0);

			eventTimes.push_back(timeTrajectory.back());
			subsystemID.push_back(eventID);

			hybridLlogicRulesMachine.push_back(partitionIndex, timeTrajectory.back(), eventID);
//			hybridLlogicRulesMachine.display();
		}

		t0 = timeTrajectory.back();

	}  // end of while loop

	// compute control input trajectory and concatenate to inputTrajectory
	for (size_t k_u=0; k_u<timeTrajectory.size(); k_u++) {
		inputTrajectory.emplace_back( systemDynamicsPtrStock_[workerIndex]->computeInput(
				timeTrajectory[k_u], stateTrajectory[k_u]) );
	} // end of k loop

	// get the guardSurfacesValues
	guardSurfacesValues = eventsPtrStock_[workerIndex]->getGuardSurfacesValues();

	return stateTrajectory.back();
}

/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::rolloutStateTriggeredTrajectory(
		const scalar_t& initTime,
		const state_vector_t& initState,
		const scalar_t& finalTime,
		const scalar_array_t& partitioningTimes,
		const controller_array_t& controllersStock,
		std::vector<scalar_array_t>& timeTrajectoriesStock,
		std::vector<size_array_t>& eventsPastTheEndIndecesStock,
		state_vector_array2_t& stateTrajectoriesStock,
		input_vector_array2_t& inputTrajectoriesStock,
		size_t threadId /*= 0*/)  {

	size_t numPartitions = partitioningTimes.size()-1;

	if (controllersStock.size() != numPartitions)
		throw std::runtime_error("controllersStock has less controllers then the number of subsystems");

	timeTrajectoriesStock.resize(numPartitions);
	eventsPastTheEndIndecesStock.resize(numPartitions);
	stateTrajectoriesStock.resize(numPartitions);
	inputTrajectoriesStock.resize(numPartitions);

	// finding the active subsystem index at initTime
	size_t initActivePartition = findActivePartitionIndex(partitioningTimes, initTime);
	// finding the active subsystem index at initTime
	size_t finalActivePartition = findActivePartitionIndex(partitioningTimes, finalTime);

	std::vector<scalar_array_t> eventTimesStock(numPartitions);
	std::vector<scalar_array_t> switchingTimesStock(numPartitions);
	std::vector<size_array_t>   switchedSystemIDsStock(numPartitions);


	scalar_array_t eventTimes;
	size_array_t subsystemID;

	hybrid_logic_rules_machine_t hybridLlogicRulesMachine;
	hybridLlogicRulesMachine.setupLogicMachine(partitioningTimes, initTime, initActivePartition, 15);

	scalar_t t0 = initTime;
	state_vector_t x0 = initState;
	scalar_array_t guardSurfacesValues;
	scalar_t tf;
	for (size_t i=0; i<numPartitions; i++)  {

		// for subsystems before the initial time
		if (i<initActivePartition  ||  i>finalActivePartition) {
			timeTrajectoriesStock[i].clear();
			eventsPastTheEndIndecesStock[i].clear();
			stateTrajectoriesStock[i].clear();
			inputTrajectoriesStock[i].clear();
			continue;
		}

		// final time
		tf = (i != finalActivePartition) ? partitioningTimes[i+1] : finalTime;

		// call rolloutStateTriggeredWorker for the partition 'i' on the thread 'threadId'
		x0 = rolloutStateTriggeredWorker(threadId, i,
				t0, x0, tf, controllersStock[i],
				timeTrajectoriesStock[i], eventsPastTheEndIndecesStock[i],
				stateTrajectoriesStock[i], inputTrajectoriesStock[i],
				eventTimes, subsystemID, guardSurfacesValues,
				hybridLlogicRulesMachine);

		// reset the initial time
		t0 = timeTrajectoriesStock[i].back();

	}  // end of i loop

	if (x0 != x0)
		throw std::runtime_error("System became unstable during the SLQ rollout.");

	hybridLlogicRulesMachine.display();
}

/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::state_vector_t
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::rolloutTimeTriggeredWorker(
			size_t workerIndex,
			const size_t& partitionIndex,
			const scalar_t& initTime,
			const state_vector_t& initState,
			const scalar_t& finalTime,
			const controller_t& controller,
			scalar_array_t& timeTrajectory,
			size_array_t& eventsPastTheEndIndeces,
			state_vector_array_t& stateTrajectory,
			input_vector_array_t& inputTrajectory)  {

	if (initTime > finalTime)
		throw std::runtime_error("Initial time should be less-equal to final time.");

	const size_t numEvents = logicRulesMachinePtr_->getNumEvents(partitionIndex);
	const size_t numSubsystems = logicRulesMachinePtr_->getNumEventCounters(partitionIndex);
	const scalar_array_t& switchingTimes = logicRulesMachinePtr_->getSwitchingTimes(partitionIndex);

	// max number of steps for integration
	const size_t maxNumSteps = settings_.maxNumStepsPerSecond_ * std::max(1.0, finalTime-initTime);

	// index of the first subsystem
	size_t beginItr = 0;
	if (partitionIndex==initActivePartition_)
		beginItr = findActiveIntervalIndex(switchingTimes, initTime, 0);
	// index of the last subsystem
	size_t finalItr = numSubsystems-1;
	if (partitionIndex==finalActivePartition_)
		finalItr = findActiveIntervalIndex(switchingTimes, finalTime, numSubsystems-1);

	// clearing the output trajectories
	timeTrajectory.clear();
	timeTrajectory.reserve(maxNumSteps+1);
	stateTrajectory.clear();
	stateTrajectory.reserve(maxNumSteps+1);
	inputTrajectory.clear();
	inputTrajectory.reserve(maxNumSteps+1);
	eventsPastTheEndIndeces.clear();
	eventsPastTheEndIndeces.reserve(numEvents);

	// initialize the model and set controller
	if (controller.empty()==false) {
		// set controller
		systemDynamicsPtrStock_[workerIndex]->setController(controller);
		// reset function calls counter
		systemDynamicsPtrStock_[workerIndex]->resetNumFunctionCalls();
		// Reset the event class
		systemEventHandlersPtrStock_[workerIndex]->reset();
		// initialize subsystem
		systemDynamicsPtrStock_[workerIndex]->initializeModel(*logicRulesMachinePtr_, partitionIndex, "SLQ");

	} else {
		// initialize operatingTrajectories
		operatingTrajectoriesPtrStock_[workerIndex]->initializeModel(*logicRulesMachinePtr_, partitionIndex, "SLQ");
	}

	state_vector_t beginState = initState;
	scalar_t beginTime, endTime;
	size_t k_u = 0;  // control input iterator
	for (size_t i=beginItr; i<=finalItr; i++) {

		beginTime = i==beginItr ? initTime  : switchingTimes[i];
		endTime   = i==finalItr ? finalTime : switchingTimes[i+1];

		// simulate subsystem
		if (controller.empty()==false) {
			// integrate controlled system
			dynamicsIntegratorsPtrStock_[workerIndex]->integrate(beginState, beginTime, endTime,
					stateTrajectory, timeTrajectory,
					settings_.minTimeStep_, settings_.absTolODE_, settings_.relTolODE_, maxNumSteps, true);
			// compute control input trajectory and concatenate to inputTrajectory
			for ( ; k_u<timeTrajectory.size(); k_u++) {
				inputTrajectory.emplace_back( systemDynamicsPtrStock_[workerIndex]->computeInput(
						timeTrajectory[k_u], stateTrajectory[k_u]) );
			} // end of k loop

		} else { // if no controller is assigned
			// get operating trajectories
			operatingTrajectoriesPtrStock_[workerIndex]->getSystemOperatingTrajectories(beginState, beginTime, endTime,
					timeTrajectory, stateTrajectory, inputTrajectory,
					true);
		}

		if (i<finalItr) {
			eventsPastTheEndIndeces.push_back( stateTrajectory.size() );
			systemDynamicsPtrStock_[workerIndex]->computeJumpMap(timeTrajectory.back(), stateTrajectory.back(), beginState);
		}

	}  // end of i loop

	// If an event has happened at the final time push it to the eventsPastTheEndIndeces
	// numEvents>finalItr means that there the final active subsystem is before an event time.
	// Note: we don't push the state because that the input might not be defined since no control policy is available)
	bool eventAtFinalTime = numEvents>finalItr &&
			logicRulesMachinePtr_->getEventTimes(partitionIndex)[finalItr]<finalTime+OCS2NumericTraits<scalar_t>::limit_epsilon();
	if (eventAtFinalTime) {
		eventsPastTheEndIndeces.push_back( stateTrajectory.size() );
		systemDynamicsPtrStock_[workerIndex]->computeJumpMap(timeTrajectory.back(), stateTrajectory.back(), beginState);
		return beginState;

	} else {
		return stateTrajectory.back();
	}
}

/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::scalar_t
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::rolloutTrajectory(
		const scalar_t& initTime,
		const state_vector_t& initState,
		const scalar_t& finalTime,
		const scalar_array_t& partitioningTimes,
		const controller_array_t& controllersStock,
		std::vector<scalar_array_t>& timeTrajectoriesStock,
		std::vector<size_array_t>& eventsPastTheEndIndecesStock,
		state_vector_array2_t& stateTrajectoriesStock,
		input_vector_array2_t& inputTrajectoriesStock,
		size_t threadId /*= 0*/)  {

	size_t numPartitions = partitioningTimes.size()-1;

	if (controllersStock.size() != numPartitions)
		throw std::runtime_error("controllersStock has less controllers then the number of subsystems");

	timeTrajectoriesStock.resize(numPartitions);
	eventsPastTheEndIndecesStock.resize(numPartitions);
	stateTrajectoriesStock.resize(numPartitions);
	inputTrajectoriesStock.resize(numPartitions);

	// finding the active subsystem index at initTime
	size_t initActivePartition = findActivePartitionIndex(partitioningTimes, initTime);
	// finding the active subsystem index at initTime
	size_t finalActivePartition = findActivePartitionIndex(partitioningTimes, finalTime);

	scalar_t t0 = initTime;
	state_vector_t x0 = initState;
	scalar_t tf;
	size_t numSteps = 0;
	for (size_t i=0; i<numPartitions; i++)  {

		// for subsystems before the initial time
		if (i<initActivePartition || i>finalActivePartition) {
			timeTrajectoriesStock[i].clear();
			eventsPastTheEndIndecesStock[i].clear();
			stateTrajectoriesStock[i].clear();
			inputTrajectoriesStock[i].clear();
			continue;
		}

		// final time
		tf = (i != finalActivePartition) ? partitioningTimes[i+1] : finalTime;

		// if blockwiseMovingHorizon_ is not set, use the previous partition's controller for
		// the first rollout of the partition. However for the very first run of the SLQ
		// it will still use operating trajectories if an initial controller is not provided.
		const controller_t* controllerPtrTemp = &controllersStock[i];
		if (blockwiseMovingHorizon_==false)
			if (controllerPtrTemp->empty()==true && i>0 && controllersStock[i-1].empty()==false)
				controllerPtrTemp = &controllersStock[i-1];

		// call rollout worker for the partition 'i' on the thread 'threadId'
		x0 = rolloutTimeTriggeredWorker(threadId, i,
				t0, x0, tf, *controllerPtrTemp,
				timeTrajectoriesStock[i], eventsPastTheEndIndecesStock[i],
				stateTrajectoriesStock[i], inputTrajectoriesStock[i]);

		// reset the initial time
		t0 = timeTrajectoriesStock[i].back();

		// total number of steps
		numSteps += timeTrajectoriesStock[i].size();

	}  // end of i loop

	if (x0 != x0)
		throw std::runtime_error("System became unstable during the SLQ rollout.");

	// average time step
	return (finalTime-initTime)/(scalar_t)numSteps;
}

/******************************************************************************************************/
// TODO (speed improvement): inputs are not necessary to be computed for all the time steps
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::rolloutFinalState (
		const scalar_t& initTime,
		const state_vector_t& initState,
		const scalar_t& finalTime,
		const scalar_array_t& partitioningTimes,
		const controller_array_t& controllersStock,
		state_vector_t& finalState,
		input_vector_t& finalInput,
		size_t& finalActivePartition,
		size_t threadId /*= 0*/) {

	size_t numPartitions = partitioningTimes.size()-1;

	if (controllersStock.size() != numPartitions)
		throw std::runtime_error("controllersStock has less controllers then the number of subsystems");

	scalar_array_t 			timeTrajectory;
	size_array_t 			eventsPastTheEndIndeces;
	state_vector_array_t 	stateTrajectory;
	input_vector_t 		inputTrajectory;

	// finding the active subsystem index at initTime and final time
	size_t initActivePartition = findActivePartitionIndex(partitioningTimes, initTime);
	finalActivePartition = findActivePartitionIndex(partitioningTimes, finalTime);

	scalar_t t0 = initTime, tf;
	state_vector_t x0 = initState;
	for (size_t i=initActivePartition; i<=finalActivePartition; i++) {

		timeTrajectory.clear();
		stateTrajectory.clear();
		inputTrajectory.clear();

		// final time
		tf = (i != finalActivePartition) ? partitioningTimes[i+1] : finalTime;

		// if blockwiseMovingHorizon_ is not set, use the previous partition's controller for
		// the first rollout of the partition. However for the very first run of the SLQ
		// it will still use operating trajectories if an initial controller is not provided.
		const controller_t* controllerPtrTemp = &controllersStock[i];
		if (blockwiseMovingHorizon_==false)
			if (controllerPtrTemp->empty()==true && i>0 && controllersStock[i-1].empty()==false)
				controllerPtrTemp = &controllersStock[i-1];

		// call rolloutTimeTriggeredWorker for the partition 'i' on the thread 'threadId'
		x0 = rolloutTimeTriggeredWorker(threadId, i,
				t0, x0, tf, *controllerPtrTemp,
				timeTrajectory, eventsPastTheEndIndeces,
				stateTrajectory, inputTrajectory);

		// reset the initial time
		t0 = timeTrajectory.back();
	}

	if (x0 != x0)
		throw std::runtime_error("System became unstable during the SLQ rollout.");

	// final state and input
	finalState = stateTrajectory.back();
	finalInput = inputTrajectory.back();
}


/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::calculateConstraintsWorker(
		size_t workerIndex,
		const size_t& partitionIndex,
		const scalar_array_t& timeTrajectory,
		const size_array_t& eventsPastTheEndIndeces,
		const state_vector_array_t& stateTrajectory,
		const input_vector_array_t& inputTrajectory,
		size_array_t& nc1Trajectory,
		constraint1_vector_array_t& EvTrajectory,
		size_array_t& nc2Trajectory,
		constraint2_vector_array_t& HvTrajectory,
		size_array_t& nc2Finals,
		constraint2_vector_array_t& HvFinals) {

	size_t N = timeTrajectory.size();

	// initialize subsystem i constraint
	if (N>0)
		systemConstraintsPtrStock_[workerIndex]->initializeModel(*logicRulesMachinePtr_, partitionIndex, "SLQ");

	// constraint type 1 computations which consists of number of active constraints at each time point
	// and the value of the constraint (if the rollout is constrained the value is always zero otherwise
	// it is nonzero)
	nc1Trajectory.resize(N);
	EvTrajectory.resize(N);

	// constraint type 2 computations which consists of number of active constraints at each time point
	// and the value of the constraint
	nc2Trajectory.resize(N);
	HvTrajectory.resize(N);

	nc2Finals.clear();
	nc2Finals.reserve(eventsPastTheEndIndeces.size());
	HvFinals.clear();
	HvFinals.reserve(eventsPastTheEndIndeces.size());

	auto eventsPastTheEndItr = eventsPastTheEndIndeces.begin();

	// compute constraint1 trajectory for subsystem i
	for (size_t k=0; k<N; k++) {

		// set data
		systemConstraintsPtrStock_[workerIndex]->setCurrentStateAndControl(
				timeTrajectory[k], stateTrajectory[k], inputTrajectory[k]);

		// constraint 1 type
		nc1Trajectory[k] = systemConstraintsPtrStock_[workerIndex]->numStateInputConstraint(timeTrajectory[k]);
		systemConstraintsPtrStock_[workerIndex]->getConstraint1(EvTrajectory[k]);
		if (nc1Trajectory[k] > INPUT_DIM)
			throw std::runtime_error("Number of active type-1 constraints should be less-equal to the number of input dimension.");

		// constraint type 2
		nc2Trajectory[k] = systemConstraintsPtrStock_[workerIndex]->numStateOnlyConstraint(timeTrajectory[k]);
		systemConstraintsPtrStock_[workerIndex]->getConstraint2(HvTrajectory[k]);
		if (nc2Trajectory[k] > INPUT_DIM)
			throw std::runtime_error("Number of active type-2 constraints should be less-equal to the number of input dimension.");

		// switching time state-constrinats
		if (eventsPastTheEndItr!=eventsPastTheEndIndeces.end() && k+1==*eventsPastTheEndItr) {
			size_t nc2Final;
			constraint2_vector_t HvFinal;
			nc2Final = systemConstraintsPtrStock_[workerIndex]->numStateOnlyFinalConstraint(timeTrajectory[k]);
			systemConstraintsPtrStock_[workerIndex]->getFinalConstraint2(HvFinal);
			if (nc2Final > INPUT_DIM)
				throw std::runtime_error("Number of active type-2 constraints at final time should be less-equal to the number of input dimension.");

			nc2Finals.push_back(std::move( nc2Final ));
			HvFinals.push_back(std::move( HvFinal ));
			eventsPastTheEndItr++;
		}

	}  // end of k loop
}


/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::calculateRolloutConstraints(
		const std::vector<scalar_array_t>& timeTrajectoriesStock,
		const std::vector<size_array_t>& eventsPastTheEndIndecesStock,
		const state_vector_array2_t& stateTrajectoriesStock,
		const input_vector_array2_t& inputTrajectoriesStock,
		std::vector<size_array_t>& nc1TrajectoriesStock,
		constraint1_vector_array2_t& EvTrajectoryStock,
		std::vector<size_array_t>& nc2TrajectoriesStock,
		constraint2_vector_array2_t& HvTrajectoryStock,
		std::vector<size_array_t>& nc2FinalStock,
		constraint2_vector_array2_t& HvFinalStock,
		size_t threadId /*= 0*/) {

	// calculate constraint violations
	// constraint type 1 computations which consists of number of active constraints at each time point
	// and the value of the constraint (if the rollout is constrained the value is always zero otherwise
	// it is nonzero)
	nc1TrajectoriesStock.resize(numPartitions_);
	EvTrajectoryStock.resize(numPartitions_);

	// constraint type 2 computations which consists of number of active constraints at each time point
	// and the value of the constraint
	nc2TrajectoriesStock.resize(numPartitions_);
	HvTrajectoryStock.resize(numPartitions_);
	nc2FinalStock.resize(numPartitions_);
	HvFinalStock.resize(numPartitions_);

	for (size_t i=0; i<numPartitions_; i++) {

		calculateConstraintsWorker(threadId, i,
				timeTrajectoriesStock[i], eventsPastTheEndIndecesStock[i],
				stateTrajectoriesStock[i], inputTrajectoriesStock[i],
				nc1TrajectoriesStock[i], EvTrajectoryStock[i],
				nc2TrajectoriesStock[i], HvTrajectoryStock[i],
				nc2FinalStock[i], HvFinalStock[i]);
	}  // end of i loop
}



/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::scalar_t
SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::calculateConstraintISE(
		const std::vector<scalar_array_t>& timeTrajectoriesStock,
		const std::vector<std::vector<size_t>>& nc1TrajectoriesStock,
		const constraint1_vector_array2_t& EvTrajectoriesStock,
		scalar_t& constraintISE)  {

	constraintISE = 0.0;
	double maxConstraintNorm = 0.0;

	scalar_t currentSquaredNormError;
	scalar_t nextSquaredNormError;

	for (size_t i=0; i<numPartitions_; i++)  {

		currentSquaredNormError = 0.0;
		nextSquaredNormError = 0.0;

		for (size_t k=0; k+1<timeTrajectoriesStock[i].size(); k++)  {

			if (k==0) {
				const size_t& nc1 = nc1TrajectoriesStock[i][0];
				if (nc1>0)
					currentSquaredNormError = EvTrajectoriesStock[i][0].head(nc1).squaredNorm();
				else
					currentSquaredNormError = 0.0;
			} else
				currentSquaredNormError = nextSquaredNormError;

			maxConstraintNorm = ((maxConstraintNorm<currentSquaredNormError)? currentSquaredNormError: maxConstraintNorm);

			const size_t& nc1 = nc1TrajectoriesStock[i][k+1];
			if (nc1>0)
				nextSquaredNormError = EvTrajectoriesStock[i][k+1].head(nc1).squaredNorm();
			else
				nextSquaredNormError = 0.0;

			constraintISE += 0.5 * (currentSquaredNormError+nextSquaredNormError) * (timeTrajectoriesStock[i][k+1]-timeTrajectoriesStock[i][k]);

		}  // end of k loop
	}  // end of i loop

	return sqrt(maxConstraintNorm);
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::calculateCostWorker(
		size_t workerIndex,
		const size_t& partitionIndex,
		const scalar_array_t& timeTrajectory,
		const size_array_t& eventsPastTheEndIndeces,
		const state_vector_array_t& stateTrajectory,
		const input_vector_array_t& inputTrajectory,
		scalar_t& totalCost)  {

	totalCost = 0.0;

	// initialize subsystem i cost
	costFunctionsPtrStock_[workerIndex]->initializeModel(*logicRulesMachinePtr_, partitionIndex, "SLQ");
	// set desired trajectories
	costFunctionsPtrStock_[workerIndex]->setCostDesiredTrajectories(costDesiredTrajectories_);

	auto eventsPastTheEndItr = eventsPastTheEndIndeces.begin();

	// integrates the intermediate cost using the trapezoidal approximation method
	scalar_t prevIntermediateCost = 0.0;
	scalar_t currIntermediateCost = 0.0;
	for (size_t k=0; k<timeTrajectory.size(); k++) {

		if (k>0)
			prevIntermediateCost = currIntermediateCost;

		// feed state and control to cost function
		costFunctionsPtrStock_[workerIndex]->setCurrentStateAndControl(
				timeTrajectory[k], stateTrajectory[k], inputTrajectory[k]);
		// getIntermediateCost intermediate cost for next time step
		costFunctionsPtrStock_[workerIndex]->getIntermediateCost(currIntermediateCost);

		if (k>0)
			totalCost += 0.5*(prevIntermediateCost+currIntermediateCost)*(timeTrajectory[k]-timeTrajectory[k-1]);

		// terminal cost at switching times
		if (eventsPastTheEndItr!=eventsPastTheEndIndeces.end() && k+1==*eventsPastTheEndItr) {
			scalar_t finalCost;
			costFunctionsPtrStock_[workerIndex]->getTerminalCost(finalCost);
			totalCost += finalCost;

			eventsPastTheEndItr++;
		}

	}  // end of k loop
}

/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::calculateRolloutCost(
		const std::vector<scalar_array_t>& timeTrajectoriesStock,
		const std::vector<size_array_t>& eventsPastTheEndIndecesStock,
		const state_vector_array2_t& stateTrajectoriesStock,
		const input_vector_array2_t& inputTrajectoriesStock,
		scalar_t& totalCost,
		size_t threadId /*= 0*/)  {

	totalCost = 0.0;

	for (size_t i=0; i<numPartitions_; i++) {
		scalar_t cost;
		calculateCostWorker(threadId, i,
				timeTrajectoriesStock[i], eventsPastTheEndIndecesStock[i],
				stateTrajectoriesStock[i], inputTrajectoriesStock[i],
				cost);
		totalCost += cost;
	}  // end of i loop

	// calculate the Heuristics function at the final time
	// initialize
	heuristicsFunctionsPtrStock_[threadId]->initializeModel(*logicRulesMachinePtr_, finalActivePartition_, "SLQ");
	// set desired trajectories
	heuristicsFunctionsPtrStock_[threadId]->setCostDesiredTrajectories(costDesiredTrajectories_);
	// set state-input
	heuristicsFunctionsPtrStock_[threadId]->setCurrentStateAndControl(
			timeTrajectoriesStock[finalActivePartition_].back(),
			stateTrajectoriesStock[finalActivePartition_].back(),
			inputTrajectoriesStock[finalActivePartition_].back());
	// compute
	scalar_t sHeuristics;
	heuristicsFunctionsPtrStock_[threadId]->getTerminalCost(sHeuristics);
	totalCost += sHeuristics;
}

/*****************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::calculateRolloutCost(
		const std::vector<scalar_array_t>& timeTrajectoriesStock,
		const std::vector<size_array_t>& eventsPastTheEndIndecesStock,
		const state_vector_array2_t& stateTrajectoriesStock,
		const input_vector_array2_t& inputTrajectoriesStock,
		const scalar_t& constraint2ISE,
		const std::vector<size_array_t>& nc2FinalStock,
		const constraint2_vector_array2_t& HvFinalStock,
		scalar_t& totalCost,
		size_t threadId /*= 0*/) {

	calculateRolloutCost(timeTrajectoriesStock, eventsPastTheEndIndecesStock,
			stateTrajectoriesStock, inputTrajectoriesStock,
			totalCost,
			threadId);

	const scalar_t stateConstraintPenalty = settings_.stateConstraintPenaltyCoeff_ *
			std::pow(settings_.stateConstraintPenaltyBase_, iteration_);

	// ISE of type-2 constraint
	totalCost += 0.5 * stateConstraintPenalty * constraint2ISE;

	// final constraint type 2
	if (settings_.noStateConstraints_==false)
		for (size_t i=0; i<numPartitions_; i++) {
			for (size_t k=0; k<nc2FinalStock[i].size(); k++) {
				const size_t& nc2Final = nc2FinalStock[i][k];
				totalCost += 0.5 * stateConstraintPenalty * HvFinalStock[i][k].head(nc2Final).squaredNorm();
			}  // end of k loop
		}  // end of i loop
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::approximateOptimalControlProblem()  {

	for (size_t i=0; i<numPartitions_; i++) {

		// intermediate LQ variables
		size_t N = nominalTimeTrajectoriesStock_[i].size();

		AmTrajectoryStock_[i].resize(N);
		BmTrajectoryStock_[i].resize(N);

		nc1TrajectoriesStock_[i].resize(N);
		EvTrajectoryStock_[i].resize(N);
		CmTrajectoryStock_[i].resize(N);
		DmTrajectoryStock_[i].resize(N);
		nc2TrajectoriesStock_[i].resize(N);
		HvTrajectoryStock_[i].resize(N);
		FmTrajectoryStock_[i].resize(N);

		qTrajectoryStock_[i].resize(N);
		QvTrajectoryStock_[i].resize(N);
		QmTrajectoryStock_[i].resize(N);
		RvTrajectoryStock_[i].resize(N);
		RmTrajectoryStock_[i].resize(N);
		RmInverseTrajectoryStock_[i].resize(N);
		PmTrajectoryStock_[i].resize(N);

		// for constraints
		DmDagerTrajectoryStock_[i].resize(N);
		AmConstrainedTrajectoryStock_[i].resize(N);
		QmConstrainedTrajectoryStock_[i].resize(N);
		QvConstrainedTrajectoryStock_[i].resize(N);
		EvProjectedTrajectoryStock_[i].resize(N);
		CmProjectedTrajectoryStock_[i].resize(N);
		DmProjectedTrajectoryStock_[i].resize(N);
		if (settings_.useRiccatiSolver_==true) {
			RmConstrainedTrajectoryStock_[i].resize(N);
		} else {
			BmConstrainedTrajectoryStock_[i].resize(N);
			PmConstrainedTrajectoryStock_[i].resize(N);
			RvConstrainedTrajectoryStock_[i].resize(N);
		}

		// switching times LQ variables
		size_t NE = nominalEventsPastTheEndIndecesStock_[i].size();

		nc2FinalStock_[i].resize(NE);
		HvFinalStock_[i].resize(NE);
		FmFinalStock_[i].resize(NE);
		qFinalStock_[i].resize(NE);
		QvFinalStock_[i].resize(NE);
		QmFinalStock_[i].resize(NE);


		if (N > 0) {

			for(size_t j=0; j<settings_.nThreads_; j++) {
				// initialize subsystem i dynamics derivatives
				systemDerivativesPtrStock_[j]->initializeModel(*logicRulesMachinePtr_, i, "SLQ");
				// initialize subsystem i constraint
				systemConstraintsPtrStock_[j]->initializeModel(*logicRulesMachinePtr_, i, "SLQ");
				// initialize subsystem i cost
				costFunctionsPtrStock_[j]->initializeModel(*logicRulesMachinePtr_, i, "SLQ");
				// set desired trajectories
				costFunctionsPtrStock_[j]->setCostDesiredTrajectories(costDesiredTrajectories_);
			}  // end of j loop

			/*
			 * perform the approximateSubsystemLQ for partition i
			 */
			approximatePartitionLQ(i);

		}

	}  // end of i loop

	// calculate the Heuristics function at the final time
	heuristicsFunctionsPtrStock_[0]->initializeModel(*logicRulesMachinePtr_, finalActivePartition_, "SLQ");
	heuristicsFunctionsPtrStock_[0]->setCostDesiredTrajectories(costDesiredTrajectories_);
	heuristicsFunctionsPtrStock_[0]->setCurrentStateAndControl(
			nominalTimeTrajectoriesStock_[finalActivePartition_].back(),
			nominalStateTrajectoriesStock_[finalActivePartition_].back(),
			nominalInputTrajectoriesStock_[finalActivePartition_].back());
	heuristicsFunctionsPtrStock_[0]->getTerminalCost(sHeuristics_(0));
	heuristicsFunctionsPtrStock_[0]->getTerminalCostDerivativeState(SvHeuristics_);
	heuristicsFunctionsPtrStock_[0]->getTerminalCostSecondDerivativeState(SmHeuristics_);
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
// FIXME: optimize the temporal variables: (e.g. Eigen::MatrixXd Cm = CmTrajectoryStock_[i][k].topRows(nc1);)
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::approximateLQWorker(
		size_t workerIndex,
		const size_t& partitionIndex,
		const size_t& timeIndex)  {

	const size_t& i = partitionIndex;
	const size_t& k = timeIndex;

	/*
	 * LINEARIZE SYSTEM DYNAMICS
	 */

	// set data
	systemDerivativesPtrStock_[workerIndex]->setCurrentStateAndControl(
			nominalTimeTrajectoriesStock_[i][k],
			nominalStateTrajectoriesStock_[i][k],
			nominalInputTrajectoriesStock_[i][k]);

	// get results
	systemDerivativesPtrStock_[workerIndex]->getFlowMapDerivativeState(AmTrajectoryStock_[i][k]);
	systemDerivativesPtrStock_[workerIndex]->getFlowMapDerivativeInput(BmTrajectoryStock_[i][k]);

	/*
	 * CONSTRAINTS and LINEARIZED CONSTRAINTS
	 */

	// set data
	systemConstraintsPtrStock_[workerIndex]->setCurrentStateAndControl(
			nominalTimeTrajectoriesStock_[i][k],
			nominalStateTrajectoriesStock_[i][k],
			nominalInputTrajectoriesStock_[i][k]);

	// constraint type 1
	nc1TrajectoriesStock_[i][k] = systemConstraintsPtrStock_[workerIndex]->numStateInputConstraint(nominalTimeTrajectoriesStock_[i][k]);
	if (nc1TrajectoriesStock_[i][k] > INPUT_DIM)
		throw std::runtime_error("Number of active type-1 constraints should be less-equal to the number of input dimension.");
	// if constraint type 1 is active
	if (nc1TrajectoriesStock_[i][k] > 0) {
		systemConstraintsPtrStock_[workerIndex]->getConstraint1(EvTrajectoryStock_[i][k]);
		systemConstraintsPtrStock_[workerIndex]->getConstraint1DerivativesState(CmTrajectoryStock_[i][k]);
		systemConstraintsPtrStock_[workerIndex]->getConstraint1DerivativesControl(DmTrajectoryStock_[i][k]);
	}

	// constraint type 2
	nc2TrajectoriesStock_[i][k] = systemConstraintsPtrStock_[workerIndex]->numStateOnlyConstraint(nominalTimeTrajectoriesStock_[i][k]);
	if (nc2TrajectoriesStock_[i][k] > INPUT_DIM)
		throw std::runtime_error("Number of active type-2 constraints should be less-equal to the number of input dimension.");
	// if constraint type 2 is active
	if (nc2TrajectoriesStock_[i][k] > 0) {
		systemConstraintsPtrStock_[workerIndex]->getConstraint2(HvTrajectoryStock_[i][k]);
		systemConstraintsPtrStock_[workerIndex]->getConstraint2DerivativesState(FmTrajectoryStock_[i][k]);
	}

	/*
	 * QUADRATIC APPROXIMATION TO THE COST FUNCTION
	 */

	// set data
	costFunctionsPtrStock_[workerIndex]->setCurrentStateAndControl(
			nominalTimeTrajectoriesStock_[i][k],
			nominalStateTrajectoriesStock_[i][k],
			nominalInputTrajectoriesStock_[i][k]);

	// get results
	costFunctionsPtrStock_[workerIndex]->getIntermediateCost(qTrajectoryStock_[i][k](0));
	costFunctionsPtrStock_[workerIndex]->getIntermediateCostDerivativeState(QvTrajectoryStock_[i][k]);
	costFunctionsPtrStock_[workerIndex]->getIntermediateCostSecondDerivativeState(QmTrajectoryStock_[i][k]);
	costFunctionsPtrStock_[workerIndex]->getIntermediateCostDerivativeInput(RvTrajectoryStock_[i][k]);
	costFunctionsPtrStock_[workerIndex]->getIntermediateCostSecondDerivativeInput(RmTrajectoryStock_[i][k]);
	RmInverseTrajectoryStock_[i][k] = RmTrajectoryStock_[i][k].ldlt().solve(input_matrix_t::Identity());
	costFunctionsPtrStock_[workerIndex]->getIntermediateCostDerivativeInputState(PmTrajectoryStock_[i][k]);

	/*
	 * Modify the unconstrained LQ coefficients to constrained ones
	 */

	// constraint type 2 coefficients
	const scalar_t stateConstraintPenalty = settings_.stateConstraintPenaltyCoeff_ *
			pow(settings_.stateConstraintPenaltyBase_, iteration_);
	const size_t& nc2 = nc2TrajectoriesStock_[i][k];
	if (nc2 > 0) {
		qTrajectoryStock_[i][k]  += 0.5 * stateConstraintPenalty * HvTrajectoryStock_[i][k].head(nc2).transpose() * HvTrajectoryStock_[i][k].head(nc2);
		QvTrajectoryStock_[i][k] += stateConstraintPenalty * FmTrajectoryStock_[i][k].topRows(nc2).transpose() * HvTrajectoryStock_[i][k].head(nc2);
		QmTrajectoryStock_[i][k] += stateConstraintPenalty * FmTrajectoryStock_[i][k].topRows(nc2).transpose() * FmTrajectoryStock_[i][k].topRows(nc2);
	}

	// constraint type 1 coefficients
	const size_t& nc1 = nc1TrajectoriesStock_[i][k];
	if (nc1 == 0) {
		DmDagerTrajectoryStock_[i][k].setZero();
		EvProjectedTrajectoryStock_[i][k].setZero();
		CmProjectedTrajectoryStock_[i][k].setZero();
		DmProjectedTrajectoryStock_[i][k].setZero();

		AmConstrainedTrajectoryStock_[i][k] = AmTrajectoryStock_[i][k];
		QmConstrainedTrajectoryStock_[i][k] = QmTrajectoryStock_[i][k];
		QvConstrainedTrajectoryStock_[i][k] = QvTrajectoryStock_[i][k];
		if (settings_.useRiccatiSolver_==true) {
			RmConstrainedTrajectoryStock_[i][k] = RmTrajectoryStock_[i][k];
		} else {
			BmConstrainedTrajectoryStock_[i][k] = BmTrajectoryStock_[i][k];
			PmConstrainedTrajectoryStock_[i][k] = PmTrajectoryStock_[i][k];
			RvConstrainedTrajectoryStock_[i][k] = RvTrajectoryStock_[i][k];
		}

	} else {
		typedef Eigen::Matrix<scalar_t, Eigen::Dynamic, Eigen::Dynamic> dynamic_matrix_t;

		dynamic_matrix_t Cm = CmTrajectoryStock_[i][k].topRows(nc1);
		dynamic_matrix_t Dm = DmTrajectoryStock_[i][k].topRows(nc1);
		dynamic_matrix_t Ev = EvTrajectoryStock_[i][k].head(nc1);

		// check numerical stability_
		if (settings_.checkNumericalStability_==true && nc1>0)
			if (Dm.colPivHouseholderQr().rank()!=nc1) {
				printString(">>> WARNING: The state-input constraints are rank deficient "
						"(at time " + std::to_string(nominalTimeTrajectoriesStock_[i][k]) + ")!");
			}

		dynamic_matrix_t RmProjected =
				( Dm*RmInverseTrajectoryStock_[i][k]*Dm.transpose() ).ldlt().solve(dynamic_matrix_t::Identity(nc1,nc1));
		dynamic_matrix_t DmDager =
				RmInverseTrajectoryStock_[i][k] * Dm.transpose() * RmProjected;

		DmDagerTrajectoryStock_[i][k].leftCols(nc1) = DmDager;
		EvProjectedTrajectoryStock_[i][k] = DmDager * Ev;
		CmProjectedTrajectoryStock_[i][k] = DmDager * Cm;
		DmProjectedTrajectoryStock_[i][k] = DmDager * Dm;

		input_matrix_t DmNullSpaceProjection = input_matrix_t::Identity() - DmProjectedTrajectoryStock_[i][k];
		state_matrix_t PmTransDmDagerCm = PmTrajectoryStock_[i][k].transpose()*CmProjectedTrajectoryStock_[i][k];

		AmConstrainedTrajectoryStock_[i][k] = AmTrajectoryStock_[i][k] - BmTrajectoryStock_[i][k]*CmProjectedTrajectoryStock_[i][k];
		QmConstrainedTrajectoryStock_[i][k] = QmTrajectoryStock_[i][k] + Cm.transpose()*RmProjected*Cm - PmTransDmDagerCm - PmTransDmDagerCm.transpose();
		QvConstrainedTrajectoryStock_[i][k] = QvTrajectoryStock_[i][k] - CmProjectedTrajectoryStock_[i][k].transpose()*RvTrajectoryStock_[i][k];
		if (settings_.useRiccatiSolver_==true) {
			RmConstrainedTrajectoryStock_[i][k] = DmNullSpaceProjection.transpose() * RmTrajectoryStock_[i][k] * DmNullSpaceProjection;
		} else {
			BmConstrainedTrajectoryStock_[i][k] = BmTrajectoryStock_[i][k] * DmNullSpaceProjection;
			PmConstrainedTrajectoryStock_[i][k] = DmNullSpaceProjection.transpose() * PmTrajectoryStock_[i][k];
			RvConstrainedTrajectoryStock_[i][k] = DmNullSpaceProjection.transpose() * RvTrajectoryStock_[i][k];
		}

	}

	// making sure that constrained Qm is PSD
	if (settings_.useMakePSD_==true)
		makePSD(QmConstrainedTrajectoryStock_[i][k]);

	// if a switch took place calculate switch related variables
	size_t NE = nominalEventsPastTheEndIndecesStock_[i].size();
	for (size_t ke=0; ke<NE; ke++)  {
		if (nominalEventsPastTheEndIndecesStock_[i][ke] == k+1)  {

			/*
			 *  Final constraint type 2
			 */
			nc2FinalStock_[i][ke] = systemConstraintsPtrStock_[workerIndex]->numStateOnlyFinalConstraint(nominalTimeTrajectoriesStock_[i][k]);

			if (nc2FinalStock_[i][ke] > INPUT_DIM)
				throw std::runtime_error("Number of active final type-2 constraints should be less-equal to the number of input dimension.");
			// if final constraint type 2 is active
			if (nc2FinalStock_[i][ke] > 0) {
				systemConstraintsPtrStock_[workerIndex]->getFinalConstraint2(HvFinalStock_[i][ke]);
				systemConstraintsPtrStock_[workerIndex]->getFinalConstraint2DerivativesState(FmFinalStock_[i][ke]);
			}

			/*
			 * Final cost
			 */
			costFunctionsPtrStock_[workerIndex]->getTerminalCost(qFinalStock_[i][ke](0));
			costFunctionsPtrStock_[workerIndex]->getTerminalCostDerivativeState(QvFinalStock_[i][ke]);
			costFunctionsPtrStock_[workerIndex]->getTerminalCostSecondDerivativeState(QmFinalStock_[i][ke]);

			/*
			 * Modify the unconstrained LQ coefficients to constrained ones
			 */
			// final constraint type 2 coefficients
			const size_t& nc2 = nc2FinalStock_[i][ke];
			if (nc2 > 0) {
				qFinalStock_[i][ke]  += 0.5 * stateConstraintPenalty * HvFinalStock_[i][ke].head(nc2).transpose() * HvFinalStock_[i][ke].head(nc2);
				QvFinalStock_[i][ke] += stateConstraintPenalty * FmFinalStock_[i][ke].topRows(nc2).transpose() * HvFinalStock_[i][ke].head(nc2);
				QmFinalStock_[i][ke] += stateConstraintPenalty * FmFinalStock_[i][ke].topRows(nc2).transpose() * FmFinalStock_[i][ke].topRows(nc2);
			}

			// making sure that Qm remains PSD
			if (settings_.useMakePSD_==true)
				makePSD(QmFinalStock_[i][ke]);

			break;
		}
	}  // end of ke loop
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::calculateController() {

	for (size_t i=0; i<numPartitions_; i++)  {

		if (i<initActivePartition_ || i>finalActivePartition_) {
			nominalControllersStock_[i].clear();
			continue;
		}

		size_t N = SsTimeTrajectoryStock_[i].size();

		nominalControllersStock_[i].time_ = SsTimeTrajectoryStock_[i];
		nominalControllersStock_[i].k_.resize(N);
		nominalControllersStock_[i].uff_.resize(N);
		nominalControllersStock_[i].deltaUff_.resize(N);

		// if the partition is not active
		if (N==0)  continue;

		// initialize interpolating function
		for (size_t j = 0; j<settings_.nThreads_; j++) {

			// functions for controller
			nominalStateFunc_[j].reset();
			nominalStateFunc_[j].setTimeStamp( &(nominalTimeTrajectoriesStock_[i]) );
			nominalStateFunc_[j].setData( &(nominalStateTrajectoriesStock_[i]) );

			nominalInputFunc_[j].reset();
			nominalInputFunc_[j].setTimeStamp( &(nominalTimeTrajectoriesStock_[i]) );
			nominalInputFunc_[j].setData( &(nominalInputTrajectoriesStock_[i]) );

			BmFunc_[j].reset();
			BmFunc_[j].setTimeStamp( &(nominalTimeTrajectoriesStock_[i]) );
			BmFunc_[j].setData( &(BmTrajectoryStock_[i]) );

			PmFunc_[j].reset();
			PmFunc_[j].setTimeStamp( &(nominalTimeTrajectoriesStock_[i]) );
			PmFunc_[j].setData( &(PmTrajectoryStock_[i]) );

			RmInverseFunc_[j].reset();
			RmInverseFunc_[j].setTimeStamp( &(nominalTimeTrajectoriesStock_[i]) );
			RmInverseFunc_[j].setData( &(RmInverseTrajectoryStock_[i]) );

			RvFunc_[j].reset();
			RvFunc_[j].setTimeStamp( &(nominalTimeTrajectoriesStock_[i]) );
			RvFunc_[j].setData( &(RvTrajectoryStock_[i]) );

			EvProjectedFunc_[j].reset();
			EvProjectedFunc_[j].setTimeStamp( &(nominalTimeTrajectoriesStock_[i]) );
			EvProjectedFunc_[j].setData( &(EvProjectedTrajectoryStock_[i]) );

			CmProjectedFunc_[j].reset();
			CmProjectedFunc_[j].setTimeStamp( &(nominalTimeTrajectoriesStock_[i]) );
			CmProjectedFunc_[j].setData( &(CmProjectedTrajectoryStock_[i]) );

			DmProjectedFunc_[j].reset();
			DmProjectedFunc_[j].setTimeStamp( &(nominalTimeTrajectoriesStock_[i]) );
			DmProjectedFunc_[j].setData( &(DmProjectedTrajectoryStock_[i]) );

		}  // end of j loop

		// current partition update
		constraintStepSize_ = initialControllerDesignStock_[i] ? 0.0 : settings_.constraintStepSize_;

		/*
		 * perform the calculatePartitionController for partition i
		 */
		calculatePartitionController(i);

	}  // end of i loop
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::calculateControllerWorker (
		size_t workerIndex,
		const size_t& partitionIndex,
		const size_t& timeIndex)  {

	const size_t& i = partitionIndex;
	const size_t& k = timeIndex;
	const scalar_t& time = SsTimeTrajectoryStock_[i][k];

	// local variables
	state_vector_t nominalState;
	input_vector_t nominalInput;
	state_input_matrix_t Bm;
	input_state_matrix_t Pm;
	input_vector_t Rv;
	input_matrix_t RmInverse;
	input_vector_t EvProjected;
	input_state_matrix_t CmProjected;
	input_matrix_t DmProjected;
	control_constraint1_matrix_t DmDager;
	input_matrix_t Rm;

	// interpolate
	nominalStateFunc_[workerIndex].interpolate(time, nominalState);
	size_t greatestLessTimeStampIndex = nominalStateFunc_[workerIndex].getGreatestLessTimeStampIndex();
	nominalInputFunc_[workerIndex].interpolate(time, nominalInput, greatestLessTimeStampIndex);

	BmFunc_[workerIndex].interpolate(time, Bm, greatestLessTimeStampIndex);
	PmFunc_[workerIndex].interpolate(time, Pm, greatestLessTimeStampIndex);
	RvFunc_[workerIndex].interpolate(time, Rv, greatestLessTimeStampIndex);
	RmInverseFunc_[workerIndex].interpolate(time, RmInverse, greatestLessTimeStampIndex);
	EvProjectedFunc_[workerIndex].interpolate(time, EvProjected, greatestLessTimeStampIndex);
	CmProjectedFunc_[workerIndex].interpolate(time, CmProjected, greatestLessTimeStampIndex);
	DmProjectedFunc_[workerIndex].interpolate(time, DmProjected, greatestLessTimeStampIndex);

	input_state_matrix_t Lm  = RmInverse * (Pm + Bm.transpose()*SmTrajectoryStock_[i][k]);
	input_vector_t     Lv  = RmInverse * (Rv + Bm.transpose()*SvTrajectoryStock_[i][k]);
	input_vector_t     Lve = RmInverse * (Bm.transpose()*SveTrajectoryStock_[i][k]);

	input_matrix_t DmNullProjection = input_matrix_t::Identity()-DmProjected;
	nominalControllersStock_[i].k_[k]   = -DmNullProjection*Lm - CmProjected;
	nominalControllersStock_[i].uff_[k] = nominalInput - nominalControllersStock_[i].k_[k]*nominalState
			- constraintStepSize_ * (DmNullProjection*Lve + EvProjected);
	nominalControllersStock_[i].deltaUff_[k] = -DmNullProjection*Lv;

	// checking the numerical stability of the controller parameters
	if (settings_.checkNumericalStability_==true){
		try {
			if (nominalControllersStock_[i].k_[k] != nominalControllersStock_[i].k_[k])
				throw std::runtime_error("Feedback gains are unstable.");
			if (nominalControllersStock_[i].deltaUff_[k] != nominalControllersStock_[i].deltaUff_[k])
				throw std::runtime_error("feedForwardControl is unstable.");
		}
		catch(const std::exception& error)  {
			std::cerr << "what(): " << error.what() << " at time " << nominalControllersStock_[i].time_[k] << " [sec]." << std::endl;
		}
	}
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::lineSearchBase(bool computeISEs) {

	// display
	if (settings_.displayInfo_) {
		scalar_t maxDeltaUffNorm, maxDeltaUeeNorm;
		calculateControllerUpdateMaxNorm(maxDeltaUffNorm, maxDeltaUeeNorm);

		std::cerr << "max feedforward update norm:  " << maxDeltaUffNorm << std::endl;
		std::cerr << "max type-1 error update norm: " << maxDeltaUeeNorm << std::endl;
	}

	// catch the nominal trajectories for which the LQ problem is constructed and solved
	nominalPrevTimeTrajectoriesStock_.swap(nominalTimeTrajectoriesStock_);
	nominalPrevEventsPastTheEndIndecesStock_.swap(nominalEventsPastTheEndIndecesStock_);
	nominalPrevStateTrajectoriesStock_.swap(nominalStateTrajectoriesStock_);
	nominalPrevInputTrajectoriesStock_.swap(nominalInputTrajectoriesStock_);

	// perform one rollout while the input correction for the type-1 constraint is considered.
	avgTimeStepFP_ = rolloutTrajectory(initTime_, initState_, finalTime_,
			partitioningTimes_, nominalControllersStock_,
			nominalTimeTrajectoriesStock_, nominalEventsPastTheEndIndecesStock_,
			nominalStateTrajectoriesStock_, nominalInputTrajectoriesStock_);

	if (computeISEs==true) {
		// calculate constraint
		calculateRolloutConstraints(nominalTimeTrajectoriesStock_, nominalEventsPastTheEndIndecesStock_,
				nominalStateTrajectoriesStock_, nominalInputTrajectoriesStock_,
				nc1TrajectoriesStock_, EvTrajectoryStock_,nc2TrajectoriesStock_,
				HvTrajectoryStock_, nc2FinalStock_, HvFinalStock_);
		// calculate constraint type-1 ISE and maximum norm
		nominalConstraint1MaxNorm_ = calculateConstraintISE(
				nominalTimeTrajectoriesStock_, nc1TrajectoriesStock_, EvTrajectoryStock_,
				nominalConstraint1ISE_);
		// calculates type-2 constraint ISE and maximum norm
		nominalConstraint2MaxNorm_ = calculateConstraintISE(
				nominalTimeTrajectoriesStock_, nc2TrajectoriesStock_, HvTrajectoryStock_,
				nominalConstraint2ISE_);
	} else {
		// calculate constraint type-1 ISE and maximum norm
		nominalConstraint1ISE_ = nominalConstraint1MaxNorm_ = 0.0;
		// calculates type-2 constraint ISE and maximum norm
		nominalConstraint2ISE_ = nominalConstraint2MaxNorm_ = 0.0;
	}

	// calculates cost
	calculateRolloutCost(nominalTimeTrajectoriesStock_, nominalEventsPastTheEndIndecesStock_,
			nominalStateTrajectoriesStock_, nominalInputTrajectoriesStock_,
			nominalConstraint2ISE_, nc2FinalStock_, HvFinalStock_,
			nominalTotalCost_);

	// display
	if (settings_.displayInfo_)  {
		std::cerr << "\t learningRate 0.0 \t cost: " << nominalTotalCost_ << " \t constraint ISE: " << nominalConstraint1ISE_ << std::endl;
		std::cerr << "\t final constraint type-2:  ";
		size_t itr = 0;
		for(size_t i=initActivePartition_; i<=finalActivePartition_; i++)
			for (size_t k=0; k<nc2FinalStock_[i].size(); k++) {
				std::cerr << "[" << itr  << "]: " << HvFinalStock_[i][k].head(nc2FinalStock_[i][k]).transpose() << ",  ";
				itr++;
			}
		std::cerr << std::endl;
		std::cerr << "\t forward pass average time step: " << avgTimeStepFP_*1e+3 << " [ms]." << std::endl;
	}
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::lineSearchWorker(
		size_t workerIndex,
		scalar_t learningRate,
		scalar_t& lsTotalCost,
		scalar_t& lsConstraint1ISE, scalar_t& lsConstraint1MaxNorm,
		scalar_t& lsConstraint2ISE, scalar_t& lsConstraint2MaxNorm,
		controller_array_t& lsControllersStock,
		std::vector<scalar_array_t>& lsTimeTrajectoriesStock,
		std::vector<size_array_t>& lsEventsPastTheEndIndecesStock,
		state_vector_array2_t& lsStateTrajectoriesStock,
		input_vector_array2_t& lsInputTrajectoriesStock)  {

	// modifying uff by local increments
	for (size_t i=0; i<numPartitions_; i++)
		for (size_t k=0; k<lsControllersStock[i].time_.size(); k++)
			lsControllersStock[i].uff_[k] += learningRate * lsControllersStock[i].deltaUff_[k];

	try {

		// perform a rollout
		scalar_t avgTimeStepFP = rolloutTrajectory(initTime_, initState_, finalTime_,
				partitioningTimes_, lsControllersStock,
				lsTimeTrajectoriesStock, lsEventsPastTheEndIndecesStock,
				lsStateTrajectoriesStock, lsInputTrajectoriesStock,
				workerIndex);

		// calculate rollout constraints
		std::vector<size_array_t>   lsNc1TrajectoriesStock(numPartitions_);
		constraint1_vector_array2_t lsEvTrajectoryStock(numPartitions_);
		std::vector<size_array_t>   lsNc2TrajectoriesStock(numPartitions_);
		constraint2_vector_array2_t lsHvTrajectoryStock(numPartitions_);
		std::vector<size_array_t>	lsNc2FinalStock(numPartitions_);
		constraint2_vector_array2_t	lsHvFinalStock(numPartitions_);

		if (lsComputeISEs_==true) {
			// calculate rollout constraints
			calculateRolloutConstraints(lsTimeTrajectoriesStock, lsEventsPastTheEndIndecesStock,
					lsStateTrajectoriesStock, lsInputTrajectoriesStock,
					lsNc1TrajectoriesStock, lsEvTrajectoryStock,
					lsNc2TrajectoriesStock, lsHvTrajectoryStock, lsNc2FinalStock, lsHvFinalStock,
					workerIndex);
			// calculate constraint type-1 ISE and maximum norm
			lsConstraint1MaxNorm = calculateConstraintISE(lsTimeTrajectoriesStock,
					lsNc1TrajectoriesStock, lsEvTrajectoryStock,
					lsConstraint1ISE);
			// calculates type-2 constraint ISE and maximum norm
			lsConstraint2MaxNorm = calculateConstraintISE(lsTimeTrajectoriesStock,
					lsNc2TrajectoriesStock, lsHvTrajectoryStock,
					lsConstraint2ISE);
		} else {
			lsConstraint1ISE = lsConstraint1MaxNorm = 0.0;
			lsConstraint2ISE = lsConstraint2MaxNorm = 0.0;
		}

		// calculate rollout cost
		calculateRolloutCost(lsTimeTrajectoriesStock, lsEventsPastTheEndIndecesStock,
				lsStateTrajectoriesStock, lsInputTrajectoriesStock,
				lsConstraint2ISE, lsNc2FinalStock, lsHvFinalStock,
				lsTotalCost,
				workerIndex);

		// display
		if (settings_.displayInfo_) {
			std::string finalConstraintDisplay;
			finalConstraintDisplay = "\t [Thread" + std::to_string(workerIndex) + "] - learningRate " + std::to_string(learningRate)
					+ " \t cost: " + std::to_string(lsTotalCost) + " \t constraint ISE: " + std::to_string(lsConstraint1ISE) + "\n";
			finalConstraintDisplay += "\t final constraint type-2:   ";
			for(size_t i=0; i<numPartitions_; i++) {
				finalConstraintDisplay += "[" + std::to_string(i) + "]: ";
				for (size_t j=0; j<lsNc2FinalStock[i].size(); j++)
					for (size_t m=0; m<lsNc2FinalStock[i][j]; m++)
						finalConstraintDisplay += std::to_string(lsHvFinalStock[i][j](m)) + ", ";
				finalConstraintDisplay += "  ";
			} // end of i loop
			finalConstraintDisplay += "\n\t forward pass average time step: " + std::to_string(avgTimeStepFP*1e+3) + " [ms].";
			printString(finalConstraintDisplay);
		}

	} catch(const std::exception& error) {
		lsTotalCost  = std::numeric_limits<scalar_t>::max();
		if(settings_.displayInfo_)
			printString("\t [Thread" + std::to_string(workerIndex) + "] rollout with learningRate " +
					std::to_string(learningRate) + " is terminated.");
	}
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::solveRiccatiEquationsWorker(
		size_t workerIndex,
		const size_t& partitionIndex,
		const state_matrix_t& SmFinal,
		const state_vector_t& SvFinal,
		const eigen_scalar_t& sFinal)  {

	// set data for Riccati equations
	riccatiEquationsPtrStock_[workerIndex]->reset();
	riccatiEquationsPtrStock_[workerIndex]->resetNumFunctionCalls();
	riccatiEquationsPtrStock_[workerIndex]->setData(
			partitioningTimes_[partitionIndex],
			partitioningTimes_[partitionIndex+1],
			&nominalTimeTrajectoriesStock_[partitionIndex],
			&AmConstrainedTrajectoryStock_[partitionIndex],
			&BmTrajectoryStock_[partitionIndex],
			&qTrajectoryStock_[partitionIndex],
			&QvConstrainedTrajectoryStock_[partitionIndex],
			&QmConstrainedTrajectoryStock_[partitionIndex],
			&RvTrajectoryStock_[partitionIndex],
			&RmInverseTrajectoryStock_[partitionIndex],
			&RmConstrainedTrajectoryStock_[partitionIndex],
			&PmTrajectoryStock_[partitionIndex],
			&nominalEventsPastTheEndIndecesStock_[partitionIndex],
			&qFinalStock_[partitionIndex],
			&QvFinalStock_[partitionIndex],
			&QmFinalStock_[partitionIndex]);

	const size_t N  = nominalTimeTrajectoriesStock_[partitionIndex].size();
	const size_t NE = nominalEventsPastTheEndIndecesStock_[partitionIndex].size();
	const scalar_t scalingFactor = partitioningTimes_[partitionIndex]-partitioningTimes_[partitionIndex+1];

	// Normalized start and final time and index for Riccati equation
	scalar_t finalNormalizedTime = 1.0;
	if (partitionIndex==initActivePartition_) {
		finalNormalizedTime = (initTime_-partitioningTimes_[partitionIndex+1])  / scalingFactor;
	}
	scalar_t startNormalizedTime = 0.0;
	if (partitionIndex==finalActivePartition_) {
		startNormalizedTime = (finalTime_-partitioningTimes_[partitionIndex+1]) / scalingFactor;
	}

	// max number of steps of integration
	size_t maxNumSteps = settings_.maxNumStepsPerSecond_ * std::max(1.0, finalNormalizedTime-startNormalizedTime);

	// clear output containers
	SsNormalizedTimeTrajectoryStock_[partitionIndex].clear();
	SsNormalizedTimeTrajectoryStock_[partitionIndex].reserve(maxNumSteps);
	typename riccati_equations_t::s_vector_array_t allSsTrajectory;
	allSsTrajectory.reserve(maxNumSteps);
	SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].clear();
	SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].reserve(NE);

	// final value from the previous partition Riccati equations
	typename riccati_equations_t::s_vector_t allSsFinal;
	riccati_equations_t::convert2Vector(SmFinal, SvFinal, sFinal, allSsFinal);

	// Normalized switching times
	scalar_array_t SsNormalizedSwitchingTimes;
	SsNormalizedSwitchingTimes.reserve(NE+2);
	SsNormalizedSwitchingTimes.push_back(startNormalizedTime);
	for (int k=NE-1; k>=0; k--) {

		const size_t& index = nominalEventsPastTheEndIndecesStock_[partitionIndex][k];

		if (index==N) {
			SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].push_back( 0 );
			typename riccati_equations_t::s_vector_t allSsFinalTemp = allSsFinal;
			riccatiEquationsPtrStock_[workerIndex]->computeJumpMap(startNormalizedTime, allSsFinalTemp, allSsFinal);

		} else {

		const scalar_t& si = nominalTimeTrajectoriesStock_[partitionIndex][index];
		SsNormalizedSwitchingTimes.push_back( (si-partitioningTimes_[partitionIndex+1]) / scalingFactor );
		}
	}
	SsNormalizedSwitchingTimes.push_back(finalNormalizedTime);
	const size_t numActiveSubsystems = SsNormalizedSwitchingTimes.size()-1;

	// integrating the Riccati equations
	for (size_t i=0; i<numActiveSubsystems; i++) {

		const scalar_t& beginTime = SsNormalizedSwitchingTimes[i];
		const scalar_t& endTime   = SsNormalizedSwitchingTimes[i+1];

		// solve Riccati equations
		riccatiIntegratorPtrStock_[workerIndex]->integrate(allSsFinal, beginTime, endTime,
				allSsTrajectory, SsNormalizedTimeTrajectoryStock_[partitionIndex],
				settings_.minTimeStep_,
				settings_.absTolODE_,
				settings_.relTolODE_,
				maxNumSteps, true);

		if (i < numActiveSubsystems-1) {
			SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].push_back( allSsTrajectory.size() );
			riccatiEquationsPtrStock_[workerIndex]->computeJumpMap(endTime, allSsTrajectory.back(), allSsFinal);
		}

	}  // end of i loop


	// denormalizing time and constructing 'Sm', 'Sv', and 's'
	size_t NS = SsNormalizedTimeTrajectoryStock_[partitionIndex].size();
	SsTimeTrajectoryStock_[partitionIndex].resize(NS);
	SmTrajectoryStock_[partitionIndex].resize(NS);
	SvTrajectoryStock_[partitionIndex].resize(NS);
	sTrajectoryStock_[partitionIndex].resize(NS);
	for (size_t k=0; k<NS; k++) {
		riccati_equations_t::convert2Matrix(allSsTrajectory[NS-1-k], SmTrajectoryStock_[partitionIndex][k], SvTrajectoryStock_[partitionIndex][k], sTrajectoryStock_[partitionIndex][k]);
		SsTimeTrajectoryStock_[partitionIndex][k] = scalingFactor*SsNormalizedTimeTrajectoryStock_[partitionIndex][NS-1-k] + partitioningTimes_[partitionIndex+1];
	}  // end of k loop

	// testing the numerical stability of the Riccati equations
	if (settings_.checkNumericalStability_)
		for (int k=NS-1; k>=0; k--) {
			try {
				if (SmTrajectoryStock_[partitionIndex][k].hasNaN())  throw std::runtime_error("Sm is unstable.");
				if (SvTrajectoryStock_[partitionIndex][k].hasNaN())  throw std::runtime_error("Sv is unstable.");
				if (sTrajectoryStock_[partitionIndex][k].hasNaN())   throw std::runtime_error("s is unstable.");
			}
			catch(const std::exception& error)
			{
				std::cerr << "what(): " << error.what() << " at time " << SsTimeTrajectoryStock_[partitionIndex][k] << " [sec]." << std::endl;
				for (int kp=k; kp<k+10; kp++)  {
					if (kp >= NS) continue;
					std::cerr << "Sm[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\n"<< SmTrajectoryStock_[partitionIndex][kp].norm() << std::endl;
					std::cerr << "Sv[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\t"<< SvTrajectoryStock_[partitionIndex][kp].transpose().norm() << std::endl;
					std::cerr << "s[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]: \t"<< sTrajectoryStock_[partitionIndex][kp].transpose().norm() << std::endl;
				}
				exit(0);
			}
		}
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::solveRiccatiEquationsForNominalTimeWorker(
		size_t workerIndex,
		const size_t& partitionIndex,
		const state_matrix_t& SmFinal,
		const state_vector_t& SvFinal,
		const eigen_scalar_t& sFinal)  {

	// set data for Riccati equations
	riccatiEquationsPtrStock_[workerIndex]->reset();
	riccatiEquationsPtrStock_[workerIndex]->setData(
			partitioningTimes_[partitionIndex], partitioningTimes_[partitionIndex+1],
			&nominalTimeTrajectoriesStock_[partitionIndex],
			&AmConstrainedTrajectoryStock_[partitionIndex], &BmTrajectoryStock_[partitionIndex],
			&qTrajectoryStock_[partitionIndex], &QvConstrainedTrajectoryStock_[partitionIndex], &QmConstrainedTrajectoryStock_[partitionIndex],
			&RvTrajectoryStock_[partitionIndex], &RmInverseTrajectoryStock_[partitionIndex], &RmConstrainedTrajectoryStock_[partitionIndex],
			&PmTrajectoryStock_[partitionIndex],
			&nominalEventsPastTheEndIndecesStock_[partitionIndex],
			&qFinalStock_[partitionIndex], &QvFinalStock_[partitionIndex], &QmFinalStock_[partitionIndex]);

	const size_t N  = nominalTimeTrajectoriesStock_[partitionIndex].size();
	const size_t NE = nominalEventsPastTheEndIndecesStock_[partitionIndex].size();
	const scalar_t scalingFactor = partitioningTimes_[partitionIndex]-partitioningTimes_[partitionIndex+1];

	SsNormalizedTimeTrajectoryStock_[partitionIndex].resize(N);
	for (size_t k=0; k<N; k++) {
		SsNormalizedTimeTrajectoryStock_[partitionIndex][N-1-k] =
				(nominalTimeTrajectoriesStock_[partitionIndex][k] - partitioningTimes_[partitionIndex+1]) / scalingFactor;
	}

	// max number of steps of integration
	riccatiEquationsPtrStock_[workerIndex]->resetNumFunctionCalls();
	const size_t maxNumSteps = settings_.maxNumStepsPerSecond_ *
			std::max(1.0, SsNormalizedTimeTrajectoryStock_[partitionIndex].back()-SsNormalizedTimeTrajectoryStock_[partitionIndex].front());

	// clear output containers
	typename riccati_equations_t::s_vector_array_t allSsTrajectory;
	allSsTrajectory.reserve(maxNumSteps);
	SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].clear();
	SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].reserve(NE);

	// final value for the last Riccati equations plus final cost
	typename riccati_equations_t::s_vector_t allSsFinal;
	riccati_equations_t::convert2Vector(SmFinal, SvFinal, sFinal, allSsFinal);

	// Normalized EventsPastTheEndIndeces
	size_array_t SsNormalizedEventsPastTheEndIndeces;
	SsNormalizedEventsPastTheEndIndeces.reserve(NE+2);
	SsNormalizedEventsPastTheEndIndeces.push_back( 0 );
	for (int k=NE-1; k>=0; k--) {

		const size_t& index = nominalEventsPastTheEndIndecesStock_[partitionIndex][k];
		SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].push_back( N-index );

		// if the event took place at the end of partition only use the jump map otherwise add
		// the reverted end index to the temporal SsNormalizedEventsPastTheEndIndeces.
		// In turn, this will cause that later the flow map is called over a time internal.
		if (index==N) {
			typename riccati_equations_t::s_vector_t allSsFinalTemp = allSsFinal;
			riccatiEquationsPtrStock_[workerIndex]->computeJumpMap(SsNormalizedTimeTrajectoryStock_[partitionIndex].front(), allSsFinalTemp, allSsFinal);

		} else {

			SsNormalizedEventsPastTheEndIndeces.push_back( N-index );
		}
	}
	SsNormalizedEventsPastTheEndIndeces.push_back( N );
	const size_t numActiveSubsystems = SsNormalizedEventsPastTheEndIndeces.size()-1;

	// integrating the Riccati equations
	typename scalar_array_t::const_iterator beginTimeItr, endTimeItr;
	for (size_t i=0; i<numActiveSubsystems; i++) {

		beginTimeItr = SsNormalizedTimeTrajectoryStock_[partitionIndex].begin() + SsNormalizedEventsPastTheEndIndeces[i];
		endTimeItr   = SsNormalizedTimeTrajectoryStock_[partitionIndex].begin() + SsNormalizedEventsPastTheEndIndeces[i+1];

		// solve Riccati equations
		riccatiIntegratorPtrStock_[workerIndex]->integrate(allSsFinal, beginTimeItr, endTimeItr,
				allSsTrajectory,
				settings_.minTimeStep_, settings_.absTolODE_, settings_.relTolODE_, maxNumSteps, true);

		if (i < numActiveSubsystems-1) {
			riccatiEquationsPtrStock_[workerIndex]->computeJumpMap(*endTimeItr, allSsTrajectory.back(), allSsFinal);
		}

	}  // end of i loop


	// denormalizing time and constructing 'Sm', 'Sv', and 's'
	SsTimeTrajectoryStock_[partitionIndex] = nominalTimeTrajectoriesStock_[partitionIndex];
	SmTrajectoryStock_[partitionIndex].resize(N);
	SvTrajectoryStock_[partitionIndex].resize(N);
	sTrajectoryStock_[partitionIndex].resize(N);
	for (size_t k=0; k<N; k++) {
		riccati_equations_t::convert2Matrix(allSsTrajectory[N-1-k], SmTrajectoryStock_[partitionIndex][k], SvTrajectoryStock_[partitionIndex][k], sTrajectoryStock_[partitionIndex][k]);
	}  // end of k loop


	// testing the numerical stability of the Riccati equations
	if (settings_.checkNumericalStability_)
		for (int k=N-1; k>=0; k--) {
			try {
				if (SmTrajectoryStock_[partitionIndex][k].hasNaN())  throw std::runtime_error("Sm is unstable.");
				if (SvTrajectoryStock_[partitionIndex][k].hasNaN())  throw std::runtime_error("Sv is unstable.");
				if (sTrajectoryStock_[partitionIndex][k].hasNaN())   throw std::runtime_error("s is unstable.");
			}
			catch(const std::exception& error)
			{
				std::cerr << "what(): " << error.what() << " at time " << SsTimeTrajectoryStock_[partitionIndex][k] << " [sec]." << std::endl;
				for (int kp=k; kp<k+10; kp++)  {
					if (kp >= N) continue;
					std::cerr << "Sm[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\n"<< SmTrajectoryStock_[partitionIndex][kp].norm() << std::endl;
					std::cerr << "Sv[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\t"<< SvTrajectoryStock_[partitionIndex][kp].transpose().norm() << std::endl;
					std::cerr << "s["  << SsTimeTrajectoryStock_[partitionIndex][kp] << "]: \t"<< sTrajectoryStock_[partitionIndex][kp].transpose().norm() << std::endl;
				}
				exit(0);
			}
		}
}


/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::solveErrorRiccatiEquationWorker(
		size_t workerIndex,
		const size_t& partitionIndex,
		const state_vector_t& SveFinal)  {

	/*
	 * Type_1 constraints error correction compensation
	 */

	const size_t N  = nominalTimeTrajectoriesStock_[partitionIndex].size();
	const size_t NS = SsNormalizedTimeTrajectoryStock_[partitionIndex].size();
	const size_t NE = SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].size();
	const scalar_t scalingFactor = partitioningTimes_[partitionIndex]-partitioningTimes_[partitionIndex+1];

	// Skip calculation of the error correction term (Sve) if the constrained simulation is used for forward simulation
	if (settings_.simulationIsConstrained_==true) {
		SveTrajectoryStock_[partitionIndex].resize(NS);
		for (size_t k=0; k<NS; k++)
			SveTrajectoryStock_[partitionIndex][k].setZero();
		return;
	}

	/*
	 * Calculating the coefficients of the error equation
	 */
	SmFuncs_[workerIndex].reset();
	SmFuncs_[workerIndex].setTimeStamp( &(SsTimeTrajectoryStock_[partitionIndex]) );
	SmFuncs_[workerIndex].setData( &(SmTrajectoryStock_[partitionIndex]) );
	state_vector_array_t GvTrajectory(N);
	state_matrix_array_t GmTrajectory(N);
	state_matrix_t Sm;
	input_state_matrix_t Lm;
	for (int k=N-1; k>=0; k--) {

		// Sm
		SmFuncs_[workerIndex].interpolate(nominalTimeTrajectoriesStock_[partitionIndex][k], Sm);
		// Lm
		Lm = RmInverseTrajectoryStock_[partitionIndex][k]*(PmTrajectoryStock_[partitionIndex][k]+BmTrajectoryStock_[partitionIndex][k].transpose()*Sm);

		GmTrajectory[k] = AmConstrainedTrajectoryStock_[partitionIndex][k] -
				BmTrajectoryStock_[partitionIndex][k]*RmInverseTrajectoryStock_[partitionIndex][k]*RmConstrainedTrajectoryStock_[partitionIndex][k]*Lm;

		GvTrajectory[k] = (CmProjectedTrajectoryStock_[partitionIndex][k]-Lm).transpose()*
				RmTrajectoryStock_[partitionIndex][k]*EvProjectedTrajectoryStock_[partitionIndex][k];
	}  // end of k loop

	// set data for error equations
	errorEquationPtrStock_[workerIndex]->reset();
	errorEquationPtrStock_[workerIndex]->setData(partitioningTimes_[partitionIndex], partitioningTimes_[partitionIndex+1],
			&nominalTimeTrajectoriesStock_[partitionIndex], &GvTrajectory, &GmTrajectory);

	// max number of steps of integration
	errorEquationPtrStock_[workerIndex]->resetNumFunctionCalls();
	size_t maxNumSteps = settings_.maxNumStepsPerSecond_
			* std::max(1.0, SsNormalizedTimeTrajectoryStock_[partitionIndex].back()-SsNormalizedTimeTrajectoryStock_[partitionIndex].front());

	// clear output
	state_vector_array_t SveTrajectory(0);
	SveTrajectory.reserve(maxNumSteps);

	// final value from the previous partition Riccati equations
	state_vector_t SveFinalInternal = SveFinal;

	// Normalized EventsPastTheEndIndeces
	size_array_t SveNormalizedEventsPastTheEndIndeces;
	SveNormalizedEventsPastTheEndIndeces.reserve(NE+2);
	SveNormalizedEventsPastTheEndIndeces.push_back( 0 );
	for (size_t k=0; k<NE; k++) {

		const size_t& index = SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex][k];

		if (index==0) {
			errorEquationPtrStock_[workerIndex]->computeJumpMap(SsNormalizedTimeTrajectoryStock_[partitionIndex].front(), SveFinal, SveFinalInternal);
		} else {
			SveNormalizedEventsPastTheEndIndeces.push_back(index);
		}
	}
	SveNormalizedEventsPastTheEndIndeces.push_back( NS );
	const size_t numActiveSubsystems = SveNormalizedEventsPastTheEndIndeces.size()-1;

	// integrating the Error Riccati equation
	typename scalar_array_t::const_iterator beginTimeItr, endTimeItr;
	for (size_t i=0; i<numActiveSubsystems; i++) {

		beginTimeItr = SsNormalizedTimeTrajectoryStock_[partitionIndex].begin() + SveNormalizedEventsPastTheEndIndeces[i];
		endTimeItr   = SsNormalizedTimeTrajectoryStock_[partitionIndex].begin() + SveNormalizedEventsPastTheEndIndeces[i+1];

		// solve Riccati equations
		errorIntegratorPtrStock_[workerIndex]->integrate(SveFinalInternal, beginTimeItr, endTimeItr,
				SveTrajectory,
				settings_.minTimeStep_,
				settings_.absTolODE_,
				settings_.relTolODE_,
				maxNumSteps, true);

		if (i < numActiveSubsystems-1) {
			errorEquationPtrStock_[workerIndex]->computeJumpMap(*endTimeItr, SveTrajectory.back(), SveFinalInternal);
		}

	}  // end of i loop

	// constructing Sve
	SveTrajectoryStock_[partitionIndex].resize(NS);
	std::reverse_copy(SveTrajectory.begin(), SveTrajectory.end(), SveTrajectoryStock_[partitionIndex].begin());

	// Testing the numerical stability
	if (settings_.checkNumericalStability_==true)
		for (size_t k=0; k<NS; k++) {

			// testing the numerical stability of the Riccati error equation
			try {
				if (SveTrajectoryStock_[partitionIndex][k].hasNaN())  throw std::runtime_error("Sve is unstable");
			}
			catch(const std::exception& error) 	{
				std::cerr << "what(): " << error.what() << " at time " << SsTimeTrajectoryStock_[partitionIndex][k] << " [sec]." << std::endl;
				for (int kp=k; kp<NS; kp++){
					std::cerr << "Sve[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\t"<< SveTrajectoryStock_[partitionIndex][kp].transpose().norm() << std::endl;
				}
				for(int kp = 0; kp+1<nominalTimeTrajectoriesStock_[partitionIndex].size(); kp++){
					std::cerr << "Gm[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\t"<< GmTrajectory[kp].transpose().norm() << std::endl;
					std::cerr << "Gv[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\t"<< GvTrajectory[kp].transpose().norm() << std::endl;
				}
				exit(0);
			}
		}  // end of k loop
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::solveSlqRiccatiEquationsWorker(
		size_t workerIndex,
		const size_t& partitionIndex,
		const state_matrix_t& SmFinal,
		const state_vector_t& SvFinal,
		const eigen_scalar_t& sFinal,
		const state_vector_t& SveFinal)  {

#ifdef USE_SEPARATE_RICCATI_SOLVER

	if(settings_.useNominalTimeForBackwardPass_==true)
		solveRiccatiEquationsForNominalTimeWorker(workerIndex, partitionIndex, SmFinal, SvFinal, sFinal);
	else
		solveRiccatiEquationsWorker(workerIndex, partitionIndex, SmFinal, SvFinal, sFinal);


	solveErrorRiccatiEquationWorker(workerIndex, partitionIndex, SveFinal);

#else

	// set data for Riccati equations
	slqRiccatiEquationsPtrStock_[workerIndex]->reset();
	slqRiccatiEquationsPtrStock_[workerIndex]->setData(partitioningTimes_[partitionIndex], partitioningTimes_[partitionIndex+1],
			&nominalTimeTrajectoriesStock_[partitionIndex],
			&AmConstrainedTrajectoryStock_[partitionIndex], &BmTrajectoryStock_[partitionIndex],
			&qTrajectoryStock_[partitionIndex], &QvConstrainedTrajectoryStock_[partitionIndex], &QmConstrainedTrajectoryStock_[partitionIndex],
			&RvTrajectoryStock_[partitionIndex], &RmInverseTrajectoryStock_[partitionIndex], &RmConstrainedTrajectoryStock_[partitionIndex],
			&PmTrajectoryStock_[partitionIndex],
			&EvProjectedTrajectoryStock_[partitionIndex], &CmProjectedTrajectoryStock_[partitionIndex],
			&nominalEventsPastTheEndIndecesStock_[partitionIndex],
			&qFinalStock_[partitionIndex], &QvFinalStock_[partitionIndex], &QmFinalStock_[partitionIndex]);

	const size_t N  = nominalTimeTrajectoriesStock_[partitionIndex].size();
	const size_t NE = nominalEventsPastTheEndIndecesStock_[partitionIndex].size();
	const scalar_t scalingFactor = partitioningTimes_[partitionIndex]-partitioningTimes_[partitionIndex+1];

	// Normalized start and final time and index for reccati equation
	scalar_t finalNormalizedTime = 1.0;
	if (partitionIndex==initActivePartition_) {
		finalNormalizedTime = (initTime_-partitioningTimes_[partitionIndex+1])  / scalingFactor;
	}
	scalar_t startNormalizedTime = 0.0;
	if (partitionIndex==finalActivePartition_) {
		startNormalizedTime = (finalTime_-partitioningTimes_[partitionIndex+1]) / scalingFactor;
	}

	// max number of steps of integration
	slqRiccatiEquationsPtrStock_[workerIndex]->resetNumFunctionCalls();
	size_t maxNumSteps = settings_.maxNumStepsPerSecond_ * std::max(1.0, finalNormalizedTime-startNormalizedTime);

	// clear output containers
	SsNormalizedTimeTrajectoryStock_[partitionIndex].clear();
	SsNormalizedTimeTrajectoryStock_[partitionIndex].reserve(maxNumSteps);
	typename slq_riccati_equations_t::s_vector_array_t allSsTrajectory;
	allSsTrajectory.reserve(maxNumSteps);
	SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].clear();
	SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].reserve(NE);

	// final value from the previous partition Riccati equations
	typename slq_riccati_equations_t::s_vector_t allSsFinal;
	slq_riccati_equations_t::convert2Vector(SmFinal, SvFinal, sFinal, SveFinal, allSsFinal);

	// Normalized switching times
	scalar_array_t SsNormalizedSwitchingTimes;
	SsNormalizedSwitchingTimes.reserve(NE+2);
	SsNormalizedSwitchingTimes.push_back(startNormalizedTime);
	for (int k=NE-1; k>=0; k--) {

		const size_t& index = nominalEventsPastTheEndIndecesStock_[partitionIndex][k];

		if (index==N) {
			SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].push_back( 0 );
			typename slq_riccati_equations_t::s_vector_t allSsFinalTemp = allSsFinal;
			slqRiccatiEquationsPtrStock_[workerIndex]->computeJumpMap(startNormalizedTime, allSsFinalTemp, allSsFinal);

		} else {

		const scalar_t& si = nominalTimeTrajectoriesStock_[partitionIndex][index];
		SsNormalizedSwitchingTimes.push_back( (si-partitioningTimes_[partitionIndex+1]) / scalingFactor );
		}
	}
	SsNormalizedSwitchingTimes.push_back(finalNormalizedTime);
	const size_t numActiveSubsystems = SsNormalizedSwitchingTimes.size()-1;

	// integrating the Riccati equations
	for (size_t i=0; i<numActiveSubsystems; i++) {

		const scalar_t& beginTime = SsNormalizedSwitchingTimes[i];
		const scalar_t& endTime   = SsNormalizedSwitchingTimes[i+1];

		// solve Riccati equations
		slqRiccatiIntegratorPtrStock_[workerIndex]->integrate(allSsFinal, beginTime, endTime,
				allSsTrajectory, SsNormalizedTimeTrajectoryStock_[partitionIndex],
				settings_.minTimeStep_,
				settings_.absTolODE_,
				settings_.relTolODE_,
				maxNumSteps, true);

		if (i < numActiveSubsystems-1) {
			SsNormalizedEventsPastTheEndIndecesStock_[partitionIndex].push_back( allSsTrajectory.size() );
			slqRiccatiEquationsPtrStock_[workerIndex]->computeJumpMap(endTime, allSsTrajectory.back(), allSsFinal);
		}

	}  // end of i loop


	// denormalizing time and constructing 'Sm', 'Sv', and 's'
	size_t NS = SsNormalizedTimeTrajectoryStock_[partitionIndex].size();
	SsTimeTrajectoryStock_[partitionIndex].resize(NS);
	SmTrajectoryStock_[partitionIndex].resize(NS);
	SvTrajectoryStock_[partitionIndex].resize(NS);
	sTrajectoryStock_[partitionIndex].resize(NS);
	SveTrajectoryStock_[partitionIndex].resize(NS);
	for (size_t k=0; k<NS; k++) {
		slq_riccati_equations_t::convert2Matrix(allSsTrajectory[NS-1-k], SmTrajectoryStock_[partitionIndex][k],
				SvTrajectoryStock_[partitionIndex][k], sTrajectoryStock_[partitionIndex][k], SveTrajectoryStock_[partitionIndex][k]);
		SsTimeTrajectoryStock_[partitionIndex][k] = scalingFactor*SsNormalizedTimeTrajectoryStock_[partitionIndex][NS-1-k] + partitioningTimes_[partitionIndex+1];
	}  // end of k loop

	// testing the numerical stability of the Riccati equations
	if (settings_.checkNumericalStability_)
		for (int k=NS-1; k>=0; k--) {
			try {
				if (SmTrajectoryStock_[partitionIndex][k].hasNaN())  throw std::runtime_error("Sm is unstable.");
				if (SvTrajectoryStock_[partitionIndex][k].hasNaN())  throw std::runtime_error("Sv is unstable.");
				if (sTrajectoryStock_[partitionIndex][k].hasNaN())   throw std::runtime_error("s is unstable.");
				if (SveTrajectoryStock_[partitionIndex][k].hasNaN())  throw std::runtime_error("Sve is unstable.");
			}
			catch(const std::exception& error)
			{
				std::cerr << "what(): " << error.what() << " at time " << SsTimeTrajectoryStock_[partitionIndex][k] << " [sec]." << std::endl;
				for (int kp=k; kp<k+10; kp++)  {
					if (kp >= NS) continue;
					std::cerr << "Sm[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\n"<< SmTrajectoryStock_[partitionIndex][kp].norm() << std::endl;
					std::cerr << "Sv[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\t"<< SvTrajectoryStock_[partitionIndex][kp].transpose().norm() << std::endl;
					std::cerr << "s[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]: \t"<< sTrajectoryStock_[partitionIndex][kp].transpose().norm() << std::endl;
					std::cerr << "Sve[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\t"<< SveTrajectoryStock_[partitionIndex][kp].transpose().norm() << std::endl;
				}
				exit(0);
			}
		}
#endif
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
template<int DIM1, int DIM2>
Eigen::Matrix<typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::scalar_t, DIM1, DIM2>
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::solveLTI(
		const std::shared_ptr<IntegratorBase<DIM1*DIM2>>& firstOrderOdeIntegratorPtr,
		const Eigen::Matrix<scalar_t, DIM1, DIM2>& x0,
		const scalar_t& deltaTime) {

	// dx = A x + B u
	using lti_equation_t = LTI_Equations<DIM1, DIM2, scalar_t>;
	using vectorized_state_t = typename lti_equation_t::vectorized_state_t;
	using vectorized_state_array_t = typename lti_equation_t::vectorized_state_array_t;

	scalar_array_t timeTrajectory {0.0, deltaTime};
	vectorized_state_array_t stateTrajectory;
	stateTrajectory.reserve(2);

#if DIM2==1
	firstOrderOdeIntegrator.integrate(x0, timeTrajectory.begin(), timeTrajectory.end(),
			stateTrajectory,
			settings_.minTimeStep_, settings_.absTolODE_, settings_.relTolODE_);

	return stateTrajectory.back();
#else
	vectorized_state_t x0Vectorized;
	lti_equation_t::convert2Vector(x0, x0Vectorized);
	firstOrderOdeIntegratorPtr->integrate(x0Vectorized, timeTrajectory.begin(), timeTrajectory.end(),
			stateTrajectory,
			settings_.minTimeStep_, settings_.absTolODE_, settings_.relTolODE_);

	Eigen::Matrix<scalar_t, DIM1, DIM2> xf;
	lti_equation_t::convert2Matrix(stateTrajectory.back(), xf);

	return xf;
#endif
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
Eigen::Matrix<typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::scalar_t, 2*STATE_DIM, STATE_DIM>
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::integrateHamiltonian(
		size_t workerIndex,
		const Eigen::Matrix<scalar_t, 2*STATE_DIM, 2*STATE_DIM>& Hm,
		const Eigen::Matrix<scalar_t, 2*STATE_DIM, STATE_DIM>& x0,
		const scalar_t& deltaTime) {

	const bool useExpMethod = false;
	if (useExpMethod==true) {
//
//		Eigen::HessenbergDecomposition<Eigen::MatrixXd> hessA(DIM1);
//		hessA.compute(A);
//
//		Eigen::Matrix<double, DIM1, DIM2> xf = hessA.matrixQ()
//							* (hessA.matrixH()*deltaTime + Eigen::MatrixXd::Identity(DIM1, DIM1)*1e-3*deltaTime).exp()
//							* hessA.matrixQ().transpose() * x0;

//		Eigen::EigenSolver<Eigen::MatrixXd> esA(DIM1);
//		esA.compute(A*deltaTime, /* computeEigenvectors = */ true);
//		Eigen::VectorXcd expLamda = esA.eigenvalues().array().exp();
//		Eigen::MatrixXcd V = esA.eigenvectors();
//
//		Eigen::Matrix<double, DIM1, DIM2> xf = (V * expLamda.asDiagonal() * V.inverse()).real() * x0;

		Eigen::Matrix<scalar_t, 2*STATE_DIM, STATE_DIM> xf = (Hm*deltaTime).exp() * x0;
		return xf;

	} else {

		// set data
		static Eigen::Matrix<scalar_t, 2*STATE_DIM, STATE_DIM> GvZero = Eigen::Matrix<scalar_t, 2*STATE_DIM, STATE_DIM>::Zero();
		hamiltonianEquationPtrStock_[workerIndex]->setData(&Hm, &GvZero);

		return solveLTI<2*STATE_DIM, STATE_DIM>(hamiltonianIntegratorPtrStock_[workerIndex],
				x0, deltaTime);
	}
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
Eigen::Matrix<typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::scalar_t, STATE_DIM, 1>
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::integrateIncrement(
		size_t workerIndex,
		const Eigen::Matrix<scalar_t, STATE_DIM, STATE_DIM>& Gm,
		const Eigen::Matrix<scalar_t, STATE_DIM, 1>& Gv,
		const Eigen::Matrix<scalar_t, STATE_DIM, 1>& x0,
		const scalar_t& deltaTime) {

	// set data
	hamiltonianIncrementEquationPtrStock_[workerIndex]->setData(&Gm, &Gv);

	return solveLTI<STATE_DIM, 1>(hamiltonianIncrementIntegratorPtrStock_[workerIndex], x0, deltaTime);
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::fullRiccatiBackwardSweepWorker(
		size_t workerIndex,
		const size_t& partitionIndex,
		const state_matrix_t& SmFinal, const state_vector_t& SvFinal,
		const state_vector_t& SveFinal, const eigen_scalar_t& sFinal,
		const scalar_t& constraintStepSize)  {

	const size_t N = nominalTimeTrajectoriesStock_[partitionIndex].size();

	// Riccati parameters
	SsTimeTrajectoryStock_[partitionIndex] = nominalTimeTrajectoriesStock_[partitionIndex];
	SmTrajectoryStock_[partitionIndex].resize(N);
	SvTrajectoryStock_[partitionIndex].resize(N);
	SveTrajectoryStock_[partitionIndex].resize(N);
	sTrajectoryStock_[partitionIndex].resize(N);

	// controller parameters
	nominalControllersStock_[partitionIndex].time_ = nominalTimeTrajectoriesStock_[partitionIndex];
	nominalControllersStock_[partitionIndex].k_.resize(N);
	nominalControllersStock_[partitionIndex].uff_.resize(N);
	nominalControllersStock_[partitionIndex].deltaUff_.resize(N);

	state_matrix_t _Gm;
	state_vector_t _Gv, _Gve;
	input_state_matrix_t _Lm, _LmConstrained;
	input_vector_t _LvConstrained, _LveConstrained;
	Eigen::Matrix<scalar_t, 2*STATE_DIM, STATE_DIM> _X_H_0, _X_H_1;
	Eigen::Matrix<scalar_t, 2*STATE_DIM, 2*STATE_DIM> _H;
	_X_H_0.template topRows<STATE_DIM>().setIdentity();

	SmTrajectoryStock_[partitionIndex][N-1]  = SmFinal;
	SvTrajectoryStock_[partitionIndex][N-1]  = SvFinal;
	SveTrajectoryStock_[partitionIndex][N-1] = SveFinal;
	sTrajectoryStock_[partitionIndex][N-1]   = sFinal;


	LmFunc_(partitionIndex, N-1, _Lm);
	LmConstrainedFunc_(partitionIndex, N-1, _Lm, _LmConstrained);
	LvConstrainedFunc_(partitionIndex, N-1, _LvConstrained);
	LveConstrainedFunc_(partitionIndex, N-1, _LveConstrained);

	ControllerFunc_(partitionIndex, N-1, constraintStepSize,
				_LmConstrained, _LvConstrained, _LveConstrained);

	int remainingEvents = nominalEventsPastTheEndIndecesStock_[partitionIndex].size();
	bool eventDetected = false;
	scalar_t deltaT = 0.0;
	for (int k=N-2; k>=0; k--) {

		// check if an event is detected.
		if (remainingEvents>0 && k+1==nominalEventsPastTheEndIndecesStock_[partitionIndex][remainingEvents-1]) {
			eventDetected = true;
			remainingEvents--;
		} else {
			eventDetected = false;
		}

		deltaT = nominalTimeTrajectoriesStock_[partitionIndex][k] - nominalTimeTrajectoriesStock_[partitionIndex][k+1];

		if (eventDetected==true) {
//			if (settings_.useMakePSD_==true)  makePSD(QmFinalStock_[partitionIndex][remainingEvents]);
			SmTrajectoryStock_[partitionIndex][k] = SmTrajectoryStock_[partitionIndex][k+1] +
					QmFinalStock_[partitionIndex][remainingEvents];

		} else {
//			if (settings_.useMakePSD_==true)  makePSD(QmConstrainedTrajectoryStock_[partitionIndex][k]);
			_H.template topLeftCorner<STATE_DIM,STATE_DIM>() = AmConstrainedTrajectoryStock_[partitionIndex][k] -
					BmConstrainedTrajectoryStock_[partitionIndex][k]*RmInverseTrajectoryStock_[partitionIndex][k]*PmConstrainedTrajectoryStock_[partitionIndex][k];
			_H.template topRightCorner<STATE_DIM,STATE_DIM>() = 0.5 * BmConstrainedTrajectoryStock_[partitionIndex][k] *
					RmInverseTrajectoryStock_[partitionIndex][k] * BmConstrainedTrajectoryStock_[partitionIndex][k].transpose();
			_H.template bottomLeftCorner<STATE_DIM,STATE_DIM>() = 2.0 * (QmConstrainedTrajectoryStock_[partitionIndex][k] -
					PmConstrainedTrajectoryStock_[partitionIndex][k].transpose()*RmInverseTrajectoryStock_[partitionIndex][k]*PmConstrainedTrajectoryStock_[partitionIndex][k]);
			_H.template bottomRightCorner<STATE_DIM,STATE_DIM>() = -_H.template topLeftCorner<STATE_DIM,STATE_DIM>().transpose();

			_X_H_0.template bottomRows<STATE_DIM>() = -2.0*SmTrajectoryStock_[partitionIndex][k+1];
			_X_H_1 = integrateHamiltonian(workerIndex, _H, _X_H_0, deltaT);
			SmTrajectoryStock_[partitionIndex][k] = -0.5 * _X_H_1.template bottomRows<STATE_DIM>() * _X_H_1.template topRows<STATE_DIM>().inverse();
		}

		LmFunc_(partitionIndex, k, _Lm);
		LmConstrainedFunc_(partitionIndex, k, _Lm, _LmConstrained);

		if (eventDetected==true) {
			SvTrajectoryStock_[partitionIndex][k]  = SvTrajectoryStock_[partitionIndex][k+1] + QvFinalStock_[partitionIndex][remainingEvents];
			SveTrajectoryStock_[partitionIndex][k] = SveTrajectoryStock_[partitionIndex][k+1];
			sTrajectoryStock_[partitionIndex][k]   = sTrajectoryStock_[partitionIndex][k+1] + qFinalStock_[partitionIndex][remainingEvents];

		} else {
			_Gm  = (AmConstrainedTrajectoryStock_[partitionIndex][k] +
					BmConstrainedTrajectoryStock_[partitionIndex][k]*_LmConstrained).transpose();
			_Gv  = (QvConstrainedTrajectoryStock_[partitionIndex][k] +
					_LmConstrained.transpose()*RvConstrainedTrajectoryStock_[partitionIndex][k]);
			_Gve = (CmProjectedTrajectoryStock_[partitionIndex][k] +
					_Lm).transpose() * RmTrajectoryStock_[partitionIndex][k] * EvProjectedTrajectoryStock_[partitionIndex][k];

			SvTrajectoryStock_[partitionIndex][k]  = integrateIncrement(workerIndex,
					_Gm, _Gv,  SvTrajectoryStock_[partitionIndex][k+1], -deltaT);
			SveTrajectoryStock_[partitionIndex][k] = integrateIncrement(workerIndex,
					_Gm, _Gve, SveTrajectoryStock_[partitionIndex][k+1], -deltaT);
			sTrajectoryStock_[partitionIndex][k]   = sTrajectoryStock_[partitionIndex][k+1] -
					deltaT * qTrajectoryStock_[partitionIndex][k];
		}

		LvConstrainedFunc_(partitionIndex, k, _LvConstrained);
		LveConstrainedFunc_(partitionIndex, k, _LveConstrained);

		// controller
		ControllerFunc_(partitionIndex, k, constraintStepSize,
				_LmConstrained, _LvConstrained, _LveConstrained);

	}

	// testing the numerical stability
	if (settings_.checkNumericalStability_==true)
		for (int k=N-1; k>=0; k--) {
			// checking the numerical stability of the Riccati equations
			try {
				if (SmTrajectoryStock_[partitionIndex][k].hasNaN())   throw std::runtime_error("Sm is unstable.");
				if (SvTrajectoryStock_[partitionIndex][k].hasNaN())   throw std::runtime_error("Sv is unstable.");
				if (SveTrajectoryStock_[partitionIndex][k].hasNaN())  throw std::runtime_error("Sve is unstable.");
				if (sTrajectoryStock_[partitionIndex][k].hasNaN())    throw std::runtime_error("s is unstable.");
			}
			catch(const std::exception& error) {
				std::cerr << "what(): " << error.what() << " at time " << SsTimeTrajectoryStock_[partitionIndex][k] << " [sec]." << std::endl;
				for (int kp=k; kp<k+10; kp++)  {
					if (kp >= N) continue;
					std::cerr << "Sm[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\n"<< SmTrajectoryStock_[partitionIndex][kp].norm() << std::endl;
					std::cerr << "Sv[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\t"<< SvTrajectoryStock_[partitionIndex][kp].transpose().norm() << std::endl;
					std::cerr << "Sve[" << SsTimeTrajectoryStock_[partitionIndex][kp] << "]:\t"<< SveTrajectoryStock_[partitionIndex][kp].transpose().norm() << std::endl;
					std::cerr << "s["  << SsTimeTrajectoryStock_[partitionIndex][kp] << "]: \t"<< sTrajectoryStock_[partitionIndex][kp].transpose().norm() << std::endl;
				}
				exit(0);
			}

			// checking the numerical stability of the controller parameters
			try {
				if (nominalControllersStock_[partitionIndex].k_[k].hasNaN())        throw std::runtime_error("Feedback gains are unstable.");
				if (nominalControllersStock_[partitionIndex].uff_[k].hasNaN())      throw std::runtime_error("uff gains are unstable.");
				if (nominalControllersStock_[partitionIndex].deltaUff_[k].hasNaN()) throw std::runtime_error("deltaUff is unstable.");
			}
			catch(const std::exception& error) {
				std::cerr << "what(): " << error.what() << " at time " << nominalControllersStock_[partitionIndex].time_[k] << " [sec]." << std::endl;
				exit(0);
			}
		}
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
template <typename Derived>
bool SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::makePSD(Eigen::MatrixBase<Derived>& squareMatrix) {

	if (squareMatrix.rows() != squareMatrix.cols())
		throw std::runtime_error("Not a square matrix: makePSD() method is for square matrix.");

	Eigen::SelfAdjointEigenSolver<Derived> eig(squareMatrix, Eigen::EigenvaluesOnly);
	Eigen::VectorXd lambda = eig.eigenvalues();

	bool hasNegativeEigenValue = false;
	for (size_t j=0; j<lambda.size() ; j++)
		if (lambda(j) < 0.0) {
			hasNegativeEigenValue = true;
			lambda(j) = 1e-6;
		}

	if (hasNegativeEigenValue) {
		eig.compute(squareMatrix, Eigen::ComputeEigenvectors);
		squareMatrix = eig.eigenvectors() * lambda.asDiagonal() * eig.eigenvectors().inverse();
	} else {
		squareMatrix = 0.5*(squareMatrix+squareMatrix.transpose()).eval();
	}

	return hasNegativeEigenValue;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::calculateControllerUpdateMaxNorm(
		scalar_t& maxDeltaUffNorm,
		scalar_t& maxDeltaUeeNorm) {

	maxDeltaUffNorm = 0.0;
	maxDeltaUeeNorm = 0.0;
	for (size_t i=initActivePartition_; i<=finalActivePartition_; i++)  {

		nominalStateFunc_[0].reset();
		nominalStateFunc_[0].setTimeStamp( &(nominalTimeTrajectoriesStock_[i]) );
		nominalStateFunc_[0].setData( &(nominalStateTrajectoriesStock_[i]) );

		nominalInputFunc_[0].reset();
		nominalInputFunc_[0].setTimeStamp( &(nominalTimeTrajectoriesStock_[i]) );
		nominalInputFunc_[0].setData( &(nominalInputTrajectoriesStock_[i]) );

		for (size_t k=0; k<nominalControllersStock_[i].time_.size(); k++)  {

			maxDeltaUffNorm = std::max(maxDeltaUffNorm, nominalControllersStock_[i].deltaUff_[k].norm());

			state_vector_t nominalState;
			nominalStateFunc_[0].interpolate(nominalControllersStock_[i].time_[k], nominalState);
			size_t greatestLessTimeStampIndex = nominalStateFunc_[0].getGreatestLessTimeStampIndex();
			input_vector_t nominalInput;
			nominalInputFunc_[0].interpolate(nominalControllersStock_[i].time_[k], nominalInput, greatestLessTimeStampIndex);
			input_vector_t deltaUee = nominalInput - nominalControllersStock_[i].k_[k]*nominalState - nominalControllersStock_[i].uff_[k];
			maxDeltaUeeNorm = std::max(maxDeltaUeeNorm, deltaUee.norm());

		}  // end of k loop
	}  // end of i loop
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::printString(const std::string& text) {

	std::lock_guard<std::mutex> outputDisplayGuard(outputDisplayGuardMutex_);
	std::cerr << text << std::endl;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::printRolloutInfo()  {

	std::cerr << "optimization cost:         " << nominalTotalCost_ << std::endl;
	std::cerr << "constraint type-1 ISE:     " << nominalConstraint1ISE_ << std::endl;
	std::cerr << "constraint type-1 MaxNorm: " << nominalConstraint1MaxNorm_ << std::endl;
	std::cerr << "constraint type-2 ISE:     " << nominalConstraint2ISE_ << std::endl;
	std::cerr << "constraint type-2 MaxNorm: " << nominalConstraint2MaxNorm_ << std::endl;
	std::cerr << "final constraint type-2: 	 ";
	size_t itr = 0;
	for(size_t i=initActivePartition_; i<=finalActivePartition_; i++)
		for (size_t k=0; k<nc2FinalStock_[i].size(); k++) {
			std::cerr << "[" << itr  << "]: " << HvFinalStock_[i][k].head(nc2FinalStock_[i][k]).transpose() << ",  ";
			itr++;
		}
	std::cerr << std::endl;
	std::cerr << "forward pass average time step:  " << avgTimeStepFP_*1e+3 << " [ms]." << std::endl;
	std::cerr << "backward pass average time step: " << avgTimeStepBP_*1e+3 << " [ms]." << std::endl;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::calculateMeritFunction(
		const std::vector<scalar_array_t>& timeTrajectoriesStock,
		const std::vector<std::vector<size_t> >& nc1TrajectoriesStock,
		const constraint1_vector_array2_t& EvTrajectoryStock,
		const std::vector<std::vector<Eigen::VectorXd>>& lagrangeTrajectoriesStock,
		const scalar_t& totalCost,
		scalar_t& meritFunctionValue,
		scalar_t& constraintISE)  {

	// add cost function
	meritFunctionValue = totalCost;

	// add the L2 penalty for constraint violation
	calculateConstraintISE(timeTrajectoriesStock, nc1TrajectoriesStock, EvTrajectoryStock, constraintISE);
	double pho = 1.0;
	if (settings_.maxNumIterationsSLQ_>1)
		pho = (iteration_-1)/(settings_.maxNumIterationsSLQ_-1) * settings_.meritFunctionRho_;

	meritFunctionValue += 0.5*pho*constraintISE;

	// add the the lagrangian term for the constraint
	scalar_t currentIntermediateMerit;
	scalar_t nextIntermediateMerit;

	for (size_t i=0; i<numPartitions_; i++)
	{
		// integrates the intermediate merit using the trapezoidal approximation method
		currentIntermediateMerit = 0.0;
		nextIntermediateMerit = 0.0;
		for (size_t k=0; k+1<timeTrajectoriesStock[i].size(); k++)
		{
			if (k==0)
				currentIntermediateMerit = EvTrajectoryStock[i][k].head(nc1TrajectoriesStock[i][k]).transpose() * lagrangeTrajectoriesStock[i][k];
			else
				currentIntermediateMerit = nextIntermediateMerit;

			nextIntermediateMerit = EvTrajectoryStock[i][k+1].head(nc1TrajectoriesStock[i][k+1]).transpose() * lagrangeTrajectoriesStock[i][k+1];

			meritFunctionValue += 0.5*(currentIntermediateMerit+nextIntermediateMerit)*(timeTrajectoriesStock[i][k+1]-timeTrajectoriesStock[i][k]);
		}  // end of k loop
	}  // end of i loop

}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
size_t SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::findActivePartitionIndex(
		const scalar_array_t& partitioningTimes,
		const scalar_t& time,
		bool ceilingFunction /*= true*/) {

	int activeSubsystemIndex;
	if (ceilingFunction==true)
		activeSubsystemIndex = findActiveIntervalIndex(partitioningTimes, time, 0);
	else
		activeSubsystemIndex = findActiveIntervalIndex(partitioningTimes, time, 0,
				-OCS2NumericTraits<scalar_t>::week_epsilon());

	if (activeSubsystemIndex < 0) {
		std::string mesg = "Given time is less than the start time (i.e. givenTime < partitioningTimes.front()): "
				+ std::to_string(time) + " < " + std::to_string(partitioningTimes.front());
		throw std::runtime_error(mesg);
	}

	if (activeSubsystemIndex == partitioningTimes.size()-1) {
		std::string mesg = "Given time is greater than the final time (i.e. partitioningTimes.back() < givenTime): "
				+ std::to_string(partitioningTimes.back()) + " < " + std::to_string(time);
		throw std::runtime_error(mesg);
	}

	return (size_t)activeSubsystemIndex;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getValueFuntion (
		const scalar_t& time, const state_vector_t& state, scalar_t& valueFuntion)  {

	size_t activeSubsystem = findActivePartitionIndex(partitioningTimes_, time);

	state_matrix_t Sm;
	LinearInterpolation<state_matrix_t,Eigen::aligned_allocator<state_matrix_t> > SmFunc(
			&SsTimeTrajectoryStock_[activeSubsystem], &SmTrajectoryStock_[activeSubsystem]);
	SmFunc.interpolate(time, Sm);
	size_t greatestLessTimeStampIndex = SmFunc.getGreatestLessTimeStampIndex();

	state_vector_t Sv;
	LinearInterpolation<state_vector_t,Eigen::aligned_allocator<state_vector_t> > SvFunc(
			&SsTimeTrajectoryStock_[activeSubsystem], &SvTrajectoryStock_[activeSubsystem]);
	SvFunc.interpolate(time, Sv, greatestLessTimeStampIndex);

	eigen_scalar_t s;
	LinearInterpolation<eigen_scalar_t,Eigen::aligned_allocator<eigen_scalar_t> > sFunc(
			&SsTimeTrajectoryStock_[activeSubsystem], &sTrajectoryStock_[activeSubsystem]);
	sFunc.interpolate(time, s, greatestLessTimeStampIndex);

	state_vector_t xNominal;
	LinearInterpolation<state_vector_t,Eigen::aligned_allocator<state_vector_t> > xNominalFunc(
			&nominalTimeTrajectoriesStock_[activeSubsystem], &nominalStateTrajectoriesStock_[activeSubsystem]);
	xNominalFunc.interpolate(time, xNominal);

	state_vector_t deltaX = state-xNominal;

	valueFuntion = (s + deltaX.transpose()*Sv + 0.5*deltaX.transpose()*Sm*deltaX).eval()(0);
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
SLQ_Settings& SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::settings() {

	return settings_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::useParallelRiccatiSolverFromInitItr(bool flag) {

	useParallelRiccatiSolverFromInitItr_ = flag;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::blockwiseMovingHorizon(bool flag) {

	blockwiseMovingHorizon_ = flag;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getPerformanceIndeces(
		scalar_t& costFunction,
		scalar_t& constraint1ISE,
		scalar_t& constraint2ISE) const {

	costFunction = nominalTotalCost_;
	constraint1ISE = nominalConstraint1ISE_;
	constraint2ISE = nominalConstraint2ISE_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
size_t SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getNumIterations() const {

	return iteration_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getIterationsLog(
		eigen_scalar_array_t& iterationCost,
		eigen_scalar_array_t& iterationISE1,
		eigen_scalar_array_t& iterationISE2) const {

	iterationCost = iterationCost_;
	iterationISE1 = iterationISE1_;
	iterationISE2 = iterationISE2_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getIterationsLogPtr(
		const eigen_scalar_array_t*& iterationCostPtr,
		const eigen_scalar_array_t*& iterationISE1Ptr,
		const eigen_scalar_array_t*& iterationISE2Ptr) const {

	iterationCostPtr = &iterationCost_;
	iterationISE1Ptr = &iterationISE1_;
	iterationISE2Ptr = &iterationISE2_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
const typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::controller_array_t&
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getController() const {

	return nominalControllersStock_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getControllerPtr(
		const controller_array_t*& controllersStockPtr) const {

	controllersStockPtr = &nominalControllersStock_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::swapController(
		controller_array_t& controllersStock) {

	controllersStock.swap(nominalControllersStock_);
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
const std::vector<typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::scalar_array_t>&
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getNominalTimeTrajectories () const {

	return nominalTimeTrajectoriesStock_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
const typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::state_vector_array2_t&
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getNominalStateTrajectories () const {

	return nominalStateTrajectoriesStock_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
const typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::input_vector_array2_t&
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getNominalInputTrajectories () const  {

	return nominalInputTrajectoriesStock_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getNominalTrajectoriesPtr(
		const std::vector<scalar_array_t>*& nominalTimeTrajectoriesStockPtr,
		const state_vector_array2_t*& nominalStateTrajectoriesStockPtr,
		const input_vector_array2_t*& nominalInputTrajectoriesStockPtr) const  {

	nominalTimeTrajectoriesStockPtr  = &nominalTimeTrajectoriesStock_;
	nominalStateTrajectoriesStockPtr = &nominalStateTrajectoriesStock_;
	nominalInputTrajectoriesStockPtr = &nominalInputTrajectoriesStock_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::swapNominalTrajectories (
		std::vector<scalar_array_t>& nominalTimeTrajectoriesStock,
		state_vector_array2_t& nominalStateTrajectoriesStock,
		input_vector_array2_t& nominalInputTrajectoriesStock)  {

	nominalTimeTrajectoriesStock.swap(nominalTimeTrajectoriesStock_);
	nominalStateTrajectoriesStock.swap(nominalStateTrajectoriesStock_);
	nominalInputTrajectoriesStock.swap(nominalInputTrajectoriesStock_);
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::truncateConterller(
		const scalar_array_t& partitioningTimes,
		const double& initTime,
		controller_array_t& controllersStock,
		size_t& initActivePartition,
		controller_array_t& deletedcontrollersStock) {

	deletedcontrollersStock.resize(numPartitions_);
	for (size_t i=0; i<numPartitions_; i++)
		deletedcontrollersStock[i].clear();

	// finding the active subsystem index at initTime_
	initActivePartition = findActivePartitionIndex(partitioningTimes, initTime);

	// saving the deleting part and clearing controllersStock
	for (size_t i=0; i<initActivePartition; i++)
		deletedcontrollersStock[i].swap(controllersStock[i]);

	if (controllersStock[initActivePartition].time_.empty()==true)  return;

	// interpolating uff
	LinearInterpolation<input_vector_t,Eigen::aligned_allocator<input_vector_t> > uffFunc;
	uffFunc.setTimeStamp(&controllersStock[initActivePartition].time_);
	uffFunc.setData(&controllersStock[initActivePartition].uff_);
	input_vector_t uffInit;
	uffFunc.interpolate(initTime, uffInit);
	size_t greatestLessTimeStampIndex = uffFunc.getGreatestLessTimeStampIndex();

	// interpolating k
	LinearInterpolation<input_state_matrix_t,Eigen::aligned_allocator<input_state_matrix_t> > kFunc;
	kFunc.setTimeStamp(&controllersStock[initActivePartition].time_);
	kFunc.setData(&controllersStock[initActivePartition].k_);
	input_state_matrix_t kInit;
	kFunc.interpolate(initTime, kInit, greatestLessTimeStampIndex);

	// deleting the controller in the active subsystem for the subsystems before initTime
	if (greatestLessTimeStampIndex>0) {

		deletedcontrollersStock[initActivePartition].time_.resize(greatestLessTimeStampIndex+1);
		deletedcontrollersStock[initActivePartition].uff_.resize(greatestLessTimeStampIndex+1);
		deletedcontrollersStock[initActivePartition].k_.resize(greatestLessTimeStampIndex+1);
		for (size_t k=0; k<=greatestLessTimeStampIndex; k++) {
			deletedcontrollersStock[initActivePartition].time_[k] = controllersStock[initActivePartition].time_[k];
			deletedcontrollersStock[initActivePartition].uff_[k] = controllersStock[initActivePartition].uff_[k];
			deletedcontrollersStock[initActivePartition].k_[k] = controllersStock[initActivePartition].k_[k];
		}

		controllersStock[initActivePartition].time_.erase (
				controllersStock[initActivePartition].time_.begin(),
				controllersStock[initActivePartition].time_.begin()+greatestLessTimeStampIndex);
		controllersStock[initActivePartition].uff_.erase (
				controllersStock[initActivePartition].uff_.begin(),
				controllersStock[initActivePartition].uff_.begin()+greatestLessTimeStampIndex);
		controllersStock[initActivePartition].k_.erase (
				controllersStock[initActivePartition].k_.begin(),
				controllersStock[initActivePartition].k_.begin()+greatestLessTimeStampIndex);
	}

	controllersStock[initActivePartition].time_[0] = initTime;
	controllersStock[initActivePartition].uff_[0] = uffInit;
	controllersStock[initActivePartition].k_[0] = kInit;

}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::rewindOptimizer(const size_t& firstIndex) {

	// No rewind is needed
	if (firstIndex==0)  return;

	// increment rewindCounter_
	rewindCounter_ += firstIndex;

	if (firstIndex > numPartitions_)
		throw std::runtime_error("Index for rewinding is greater than the current size.");

	const size_t preservedLength = numPartitions_ - firstIndex;
	for (size_t i=0; i<numPartitions_; i++)
		if (i<preservedLength) {
			nominalControllersStock_[i].swap(nominalControllersStock_[firstIndex+i]);
			SmFinalStock_[i]  = SmFinalStock_[firstIndex+i];
			SvFinalStock_[i]  = SvFinalStock_[firstIndex+i];
			SveFinalStock_[i] = SveFinalStock_[firstIndex+i];
			sFinalStock_[i]   = sFinalStock_[firstIndex+i];
			xFinalStock_[i]   = xFinalStock_[firstIndex+i];
		} else {
			nominalControllersStock_[i].clear();
			SmFinalStock_[i].setZero();
			SvFinalStock_[i].setZero();
			SveFinalStock_[i].setZero();
			sFinalStock_[i].setZero();
			xFinalStock_[i].setZero();
		}
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
const unsigned long long int& SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getRewindCounter() const {

	return rewindCounter_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::setupOptimizer(const size_t& numPartitions) {

	if (numPartitions==0)
		throw std::runtime_error("Number of partitions cannot be zero!");

	nullDesiredTimeTrajectoryStockPtr_  = std::vector<scalar_array_t>( numPartitions, scalar_array_t() );
	nullDesiredStateTrajectoryStockPtr_ = state_vector_array2_t( numPartitions, state_vector_array_t() );
	nullDesiredInputTrajectoryStockPtr_ = input_vector_array2_t( numPartitions, input_vector_array_t() );

	/*
	 * nominal trajectories
	 */
	nominalControllersStock_.resize(numPartitions);
	nominalTimeTrajectoriesStock_.resize(numPartitions);
	nominalEventsPastTheEndIndecesStock_.resize(numPartitions);
	nominalStateTrajectoriesStock_.resize(numPartitions);
	nominalInputTrajectoriesStock_.resize(numPartitions);

	nominalPrevTimeTrajectoriesStock_.resize(numPartitions);
	nominalPrevEventsPastTheEndIndecesStock_.resize(numPartitions);
	nominalPrevStateTrajectoriesStock_.resize(numPartitions);
	nominalPrevInputTrajectoriesStock_.resize(numPartitions);

	/*
	 * Riccati solver variables and controller update
	 */
	SmFinalStock_  = state_matrix_array_t(numPartitions, state_matrix_t::Zero());
	SvFinalStock_  = state_vector_array_t(numPartitions, state_vector_t::Zero());
	SveFinalStock_ = state_vector_array_t(numPartitions, state_vector_t::Zero());
	sFinalStock_   = eigen_scalar_array_t(numPartitions, eigen_scalar_t::Zero());
	xFinalStock_   = state_vector_array_t(numPartitions, state_vector_t::Zero());

	SsTimeTrajectoryStock_.resize(numPartitions);
	SsNormalizedTimeTrajectoryStock_.resize(numPartitions);
	SsNormalizedEventsPastTheEndIndecesStock_.resize(numPartitions);
	sTrajectoryStock_.resize(numPartitions);
	SvTrajectoryStock_.resize(numPartitions);
	SveTrajectoryStock_.resize(numPartitions);
	SmTrajectoryStock_.resize(numPartitions);

	initialControllerDesignStock_.resize(numPartitions);

	/*
	 * approximate LQ variables
	 */
	AmTrajectoryStock_.resize(numPartitions);
	BmTrajectoryStock_.resize(numPartitions);

	nc1TrajectoriesStock_.resize(numPartitions);
	EvTrajectoryStock_.resize(numPartitions);
	CmTrajectoryStock_.resize(numPartitions);
	DmTrajectoryStock_.resize(numPartitions);
	nc2TrajectoriesStock_.resize(numPartitions);
	HvTrajectoryStock_.resize(numPartitions);
	FmTrajectoryStock_.resize(numPartitions);

	qTrajectoryStock_.resize(numPartitions);
	QvTrajectoryStock_.resize(numPartitions);
	QmTrajectoryStock_.resize(numPartitions);
	RvTrajectoryStock_.resize(numPartitions);
	RmTrajectoryStock_.resize(numPartitions);
	RmInverseTrajectoryStock_.resize(numPartitions);
	PmTrajectoryStock_.resize(numPartitions);

	nc2FinalStock_.resize(numPartitions);
	HvFinalStock_.resize(numPartitions);
	FmFinalStock_.resize(numPartitions);
	qFinalStock_.resize(numPartitions);
	QvFinalStock_.resize(numPartitions);
	QmFinalStock_.resize(numPartitions);

	// constraint type 1 coefficients
	DmDagerTrajectoryStock_.resize(numPartitions);
	AmConstrainedTrajectoryStock_.resize(numPartitions);
	QmConstrainedTrajectoryStock_.resize(numPartitions);
	QvConstrainedTrajectoryStock_.resize(numPartitions);
	EvProjectedTrajectoryStock_.resize(numPartitions);
	CmProjectedTrajectoryStock_.resize(numPartitions);
	DmProjectedTrajectoryStock_.resize(numPartitions);
	RmConstrainedTrajectoryStock_.resize(numPartitions);
	BmConstrainedTrajectoryStock_.resize(numPartitions);
	PmConstrainedTrajectoryStock_.resize(numPartitions);
	RvConstrainedTrajectoryStock_.resize(numPartitions);
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
const typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::scalar_t&
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getFinalTime() const {

	return finalTime_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
const typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::scalar_array_t&
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getPartitioningTimes() const {

	return partitioningTimes_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::logic_rules_machine_t*
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getLogicRulesMachinePtr() {

	return logicRulesMachinePtr_.get();
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
const typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::logic_rules_machine_t*
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getLogicRulesMachinePtr() const {

	return logicRulesMachinePtr_.get();
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::setLogicRules(const LOGIC_RULES_T& logicRules) {

	logicRulesMachinePtr_->setLogicRules(logicRules);
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
const LOGIC_RULES_T& SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getLogicRules() const {

	return logicRulesMachinePtr_->getLogicRules();
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
LOGIC_RULES_T& SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getLogicRules() {

	return logicRulesMachinePtr_->getLogicRules();
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
const typename SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::scalar_array_t&
	SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getEventTimes() const {

	return logicRulesMachinePtr_->getLogicRulesPtr()->eventTimes();
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::getCostDesiredTrajectoriesPtr(
		const cost_desired_trajectories_t*& costDesiredTrajectoriesPtr) const {

	costDesiredTrajectoriesPtr = &costDesiredTrajectories_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::setCostDesiredTrajectories(
		const cost_desired_trajectories_t& costDesiredTrajectories) {

	costDesiredTrajectoriesUpdated_ = true;
	costDesiredTrajectoriesBuffer_ = costDesiredTrajectories;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::setCostDesiredTrajectories(
		const scalar_array_t& desiredTimeTrajectory,
		const dynamic_vector_array_t& desiredStateTrajectory,
		const dynamic_vector_array_t& desiredInputTrajectory) {

	costDesiredTrajectoriesUpdated_ = true;
	costDesiredTrajectoriesBuffer_.desiredTimeTrajectory()  = desiredTimeTrajectory;
	costDesiredTrajectoriesBuffer_.desiredStateTrajectory() = desiredStateTrajectory;
	costDesiredTrajectoriesBuffer_.desiredInputTrajectory() = desiredInputTrajectory;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::swapCostDesiredTrajectories(
		cost_desired_trajectories_t& costDesiredTrajectories) {

	costDesiredTrajectoriesUpdated_ = true;
	costDesiredTrajectoriesBuffer_.swap(costDesiredTrajectories);
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::swapCostDesiredTrajectories(
		scalar_array_t& desiredTimeTrajectory,
		dynamic_vector_array_t& desiredStateTrajectory,
		dynamic_vector_array_t& desiredInputTrajectory) {

	costDesiredTrajectoriesUpdated_ = true;
	costDesiredTrajectoriesBuffer_.desiredTimeTrajectory().swap(desiredTimeTrajectory);
	costDesiredTrajectoriesBuffer_.desiredStateTrajectory().swap(desiredStateTrajectory);
	costDesiredTrajectoriesBuffer_.desiredInputTrajectory().swap(desiredInputTrajectory);
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
bool SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::costDesiredTrajectoriesUpdated() const {

	return costDesiredTrajectoriesUpdated_;
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::runInit() {

#ifdef BENCHMARK
	// Benchmarking
	BENCHMARK_nIterationsLQ_++;
	BENCHMARK_nIterationsBP_++;
	BENCHMARK_nIterationsFP_++;
	BENCHMARK_start_ = std::chrono::steady_clock::now();
#endif

	// initial controller rollout
	avgTimeStepFP_ = rolloutTrajectory(initTime_, initState_, finalTime_,
			partitioningTimes_, nominalControllersStock_,
			nominalTimeTrajectoriesStock_, nominalEventsPastTheEndIndecesStock_,
			nominalStateTrajectoriesStock_, nominalInputTrajectoriesStock_);

#ifdef BENCHMARK
	BENCHMARK_end_ = std::chrono::steady_clock::now();
	auto BENCHMARK_diff_ = BENCHMARK_end_ - BENCHMARK_start_;
	BENCHMARK_tAvgFP_ = ((1.0 - 1.0/BENCHMARK_nIterationsFP_)* BENCHMARK_tAvgFP_) +
			(1.0/BENCHMARK_nIterationsFP_)*std::chrono::duration_cast<std::chrono::milliseconds>(BENCHMARK_diff_).count();
	BENCHMARK_start_ = std::chrono::steady_clock::now();
#endif

	// linearizing the dynamics and quadratizing the cost function along nominal trajectories
	approximateOptimalControlProblem();

	// to check convergence of the main loop, we need to compute the total cost and ISEs
	bool computePerformanceIndex = settings_.displayInfo_==true || settings_.maxNumIterationsSLQ_>1;
	if (computePerformanceIndex==true) {
		// calculate rollout constraint type-1 ISE
		nominalConstraint1MaxNorm_ = calculateConstraintISE(nominalTimeTrajectoriesStock_, nc1TrajectoriesStock_, EvTrajectoryStock_,
				nominalConstraint1ISE_);
		// calculate rollout constraint type-2 ISE
		if (settings_.noStateConstraints_==false)
			nominalConstraint2MaxNorm_ = calculateConstraintISE(nominalTimeTrajectoriesStock_, nc2TrajectoriesStock_, HvTrajectoryStock_,
					nominalConstraint2ISE_);
		else
			nominalConstraint2ISE_ = nominalConstraint2MaxNorm_ = 0.0;
		// calculate rollout cost
		calculateRolloutCost(nominalTimeTrajectoriesStock_, nominalEventsPastTheEndIndecesStock_,
				nominalStateTrajectoriesStock_, nominalInputTrajectoriesStock_,
				nominalTotalCost_);
	} else {
		nominalTotalCost_ = 0.0;
		nominalConstraint1ISE_ = nominalConstraint1MaxNorm_ = 0.0;
		nominalConstraint2ISE_ = nominalConstraint2MaxNorm_ = 0.0;
	}

#ifdef BENCHMARK
	BENCHMARK_end_ = std::chrono::steady_clock::now();
	BENCHMARK_diff_ = BENCHMARK_end_ - BENCHMARK_start_;
	BENCHMARK_tAvgLQ_ = ((1.0 - 1.0/BENCHMARK_nIterationsLQ_)* BENCHMARK_tAvgLQ_) +
			(1.0/BENCHMARK_nIterationsLQ_)*std::chrono::duration_cast<std::chrono::milliseconds>(BENCHMARK_diff_).count();
	BENCHMARK_start_ = std::chrono::steady_clock::now();
#endif

	// solve Riccati equations
	avgTimeStepBP_ = solveSequentialRiccatiEquations(SmHeuristics_, SvHeuristics_, sHeuristics_);
	// calculate controller
	if (settings_.useRiccatiSolver_==true)
		calculateController();

#ifdef BENCHMARK
	BENCHMARK_end_ = std::chrono::steady_clock::now();
	BENCHMARK_diff_ = BENCHMARK_end_ - BENCHMARK_start_;
	BENCHMARK_tAvgBP_ = ((1.0 - 1.0/BENCHMARK_nIterationsBP_)* BENCHMARK_tAvgBP_) +
			(1.0/BENCHMARK_nIterationsBP_)*std::chrono::duration_cast<std::chrono::milliseconds>(BENCHMARK_diff_).count();
#endif

	// display
	if (settings_.displayInfo_)
		printRolloutInfo();
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::runIteration() {

#ifdef BENCHMARK
	// Benchmarking
	BENCHMARK_nIterationsLQ_++;
	BENCHMARK_nIterationsBP_++;
	BENCHMARK_nIterationsFP_++;
	BENCHMARK_start_ = std::chrono::steady_clock::now();
#endif

	bool computeISEs = settings_.displayInfo_==true || settings_.noStateConstraints_==false;

	// finding the optimal learningRate
	maxLearningRate_ = settings_.maxLearningRateGSLQP_;
	lineSearch(computeISEs);

#ifdef BENCHMARK
	BENCHMARK_end_ = std::chrono::steady_clock::now();
	BENCHMARK_diff_ = BENCHMARK_end_ - BENCHMARK_start_;
	BENCHMARK_tAvgFP_ = ((1.0 - 1.0/BENCHMARK_nIterationsFP_)* BENCHMARK_tAvgFP_) +
			(1.0/BENCHMARK_nIterationsFP_)*std::chrono::duration_cast<std::chrono::milliseconds>(BENCHMARK_diff_).count();
	BENCHMARK_start_ = std::chrono::steady_clock::now();
#endif

	// linearizing the dynamics and quadratizing the cost function along nominal trajectories
	approximateOptimalControlProblem();

	// to check convergence of the main loop, we need to compute ISEs
	if (computeISEs==false) {
		// calculate constraint type-1 ISE and maximum norm
		nominalConstraint1MaxNorm_ = calculateConstraintISE(nominalTimeTrajectoriesStock_, nc1TrajectoriesStock_, EvTrajectoryStock_,
				nominalConstraint1ISE_);
		// calculates type-2 constraint ISE and maximum norm
		if (settings_.noStateConstraints_==false)
		nominalConstraint2MaxNorm_ = calculateConstraintISE(nominalTimeTrajectoriesStock_, nc2TrajectoriesStock_, HvTrajectoryStock_,
				nominalConstraint2ISE_);
		else
			nominalConstraint2ISE_ = nominalConstraint2MaxNorm_ = 0.0;
	}

#ifdef BENCHMARK
	BENCHMARK_end_ = std::chrono::steady_clock::now();
	BENCHMARK_diff_ = BENCHMARK_end_ - BENCHMARK_start_;
	BENCHMARK_tAvgLQ_ = ((1.0 - 1.0/BENCHMARK_nIterationsLQ_)* BENCHMARK_tAvgLQ_) +
			(1.0/BENCHMARK_nIterationsLQ_)*std::chrono::duration_cast<std::chrono::milliseconds>(BENCHMARK_diff_).count();
	BENCHMARK_start_ = std::chrono::steady_clock::now();
#endif

	// solve Riccati equations
	avgTimeStepBP_ = solveSequentialRiccatiEquations(SmHeuristics_, SvHeuristics_, sHeuristics_);
	// calculate controller
	if (settings_.useRiccatiSolver_==true)
		calculateController();

#ifdef BENCHMARK
	BENCHMARK_end_ = std::chrono::steady_clock::now();
	BENCHMARK_diff_ = BENCHMARK_end_ - BENCHMARK_start_;
	BENCHMARK_tAvgBP_ = ((1.0 - 1.0/BENCHMARK_nIterationsBP_)* BENCHMARK_tAvgBP_) +
			(1.0/BENCHMARK_nIterationsBP_)*std::chrono::duration_cast<std::chrono::milliseconds>(BENCHMARK_diff_).count();
#endif

	// display
	if (settings_.displayInfo_)
		printRolloutInfo();
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::runExit()  {

	// add the deleted parts of the controller
	for (size_t i=0; i<initActivePartition_; i++)
		nominalControllersStock_[i].swap(deletedcontrollersStock_[i]);

	if (deletedcontrollersStock_[initActivePartition_].time_.empty()==false) {

		nominalControllersStock_[initActivePartition_].swap(deletedcontrollersStock_[initActivePartition_]);

		for (size_t k=0; k<deletedcontrollersStock_[initActivePartition_].time_.size(); k++) {
			nominalControllersStock_[initActivePartition_].time_.push_back(deletedcontrollersStock_[initActivePartition_].time_[k]);
			nominalControllersStock_[initActivePartition_].uff_.push_back(deletedcontrollersStock_[initActivePartition_].uff_[k]);
			nominalControllersStock_[initActivePartition_].k_.push_back(deletedcontrollersStock_[initActivePartition_].k_[k]);
		}  // end of k loop
	}
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::run(
		const scalar_t& initTime,
		const state_vector_t& initState,
		const scalar_t& finalTime,
		const scalar_array_t& partitioningTimes) {

	const controller_array_t noInitialController(partitioningTimes.size()-1, controller_t());

	// call the "run" method which uses the internal controllers stock (i.e. nominalControllersStock_)
	run(initTime, initState, finalTime, partitioningTimes, noInitialController);
}


/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
template <size_t STATE_DIM, size_t INPUT_DIM, class LOGIC_RULES_T>
void SLQ_BASE<STATE_DIM, INPUT_DIM, LOGIC_RULES_T>::run(
		const scalar_t& initTime,
		const state_vector_t& initState,
		const scalar_t& finalTime,
		const scalar_array_t& partitioningTimes,
		const controller_array_t& controllersStock) {

	if (settings_.displayInfo_) {
		std::cerr << "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
		std::cerr << "++++++++++++++++ SLQ solver is initialized +++++++++++++++++" << std::endl;
		std::cerr << "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
	}

	// update numPartitions_ if it has been changed
	if (numPartitions_+1 != partitioningTimes.size()) {
		numPartitions_  = partitioningTimes.size()-1;
		partitioningTimes_ = partitioningTimes;
		setupOptimizer(numPartitions_);
	}

	// update partitioningTimes_
	partitioningTimes_ = partitioningTimes;

	// Use the input controller if it is not empty otherwise use the internal controller (nominalControllersStock_).
	// In the later case 2 scenarios are possible: either the internal controller is already set (such as the MPC case
	// where the warm starting option is set true) or the internal controller is empty in which instead of performing
	// a rollout the operating trajectories will be used.
	if (controllersStock.empty()==false) {
		nominalControllersStock_ = controllersStock;
		if (controllersStock.size() != numPartitions_)
			throw std::runtime_error("controllersStock has less controllers than the number of partitions.");
	} else {
		if (nominalControllersStock_.size() != numPartitions_)
			throw std::runtime_error("The internal controller is not compatible with the number of partitions.");
	}

	// set desired trajectories of cost if it is updated
	if (costDesiredTrajectoriesUpdated_ == true) {
		costDesiredTrajectoriesUpdated_ = false;
		costDesiredTrajectories_.swap(costDesiredTrajectoriesBuffer_);
	}

	// update the LOGIC_RULES in the beginning of the run routine and adjust the nominal controllerStock based on an user-defined function
	logicRulesMachinePtr_->updateLogicRules(partitioningTimes_, nominalControllersStock_);

	// display
	if (settings_.displayInfo_) {
		std::cerr << std::endl << "Rewind Counter: " << rewindCounter_ << std::endl;
		std::cerr << "SLQ solver starts from initial time " << initTime << " to final time " << finalTime << ".";
		logicRulesMachinePtr_->display();
		std::cerr << std::endl;
	}

	iteration_ = 0;
	initState_ = initState;
	initTime_  = initTime;
	finalTime_ = finalTime;

	iterationCost_.clear();
	iterationISE1_.clear();
	iterationISE2_.clear();

	// finding the initial active partition index and truncating the controller
	truncateConterller(partitioningTimes_, initTime_,
			nominalControllersStock_, initActivePartition_, deletedcontrollersStock_);

	// the final active partition index.
	finalActivePartition_ = findActivePartitionIndex(partitioningTimes_, finalTime_);

	// check if after the truncation the internal controller is empty
	bool isInitInternalControllerEmpty = false;
	for (const controller_t& controller: nominalControllersStock_) {
		isInitInternalControllerEmpty = isInitInternalControllerEmpty || controller.empty();
	}

	// display
	if (settings_.displayInfo_)
		std::cerr << "\n#### Iteration " << iteration_ << " (Dynamics might have been violated)" << std::endl;

	// if a controller is not set for a partition
	for (size_t i=0; i<numPartitions_; i++)
		initialControllerDesignStock_[i] = nominalControllersStock_[i].empty() ? true : false;

	// run SLQ initializer and update the member variables
	runInit();

	// after iteration zero always allow feedforward policy update
	for (size_t i=0; i<numPartitions_; i++)
		initialControllerDesignStock_[i] = false;

	iterationCost_.push_back( (Eigen::VectorXd(1) << nominalTotalCost_).finished() );
	iterationISE1_.push_back( (Eigen::VectorXd(1) << nominalConstraint1ISE_).finished() );
	iterationISE2_.push_back( (Eigen::VectorXd(1) << nominalConstraint2ISE_).finished() );

	// convergence conditions variables
	scalar_t relCost;
	scalar_t relConstraint1ISE;
	bool isLearningRateStarZero = false;
	bool isCostFunctionConverged = false;
	bool isConstraint1Satisfied  = false;
	bool isOptimizationConverged = false;

	// SLQ main loop
	while (iteration_+1<settings_.maxNumIterationsSLQ_ && isOptimizationConverged==false)  {

		// increment iteration counter
		iteration_++;

		scalar_t costCashed = nominalTotalCost_;
		scalar_t constraint1ISECashed = nominalConstraint1ISE_;

		// display
		if (settings_.displayInfo_)  std::cerr << "\n#### Iteration " << iteration_ << std::endl;

		// run the an iteration of the SLQ algorithm and update the member variables
		runIteration();

		iterationCost_.push_back( (Eigen::VectorXd(1) << nominalTotalCost_).finished() );
		iterationISE1_.push_back( (Eigen::VectorXd(1) << nominalConstraint1ISE_).finished() );
		iterationISE2_.push_back( (Eigen::VectorXd(1) << nominalConstraint2ISE_).finished() );

		// loop break variables
		relCost = std::abs(nominalTotalCost_-costCashed);
		relConstraint1ISE = std::abs(nominalConstraint1ISE_-constraint1ISECashed);
		isConstraint1Satisfied  = nominalConstraint1ISE_<=settings_.minAbsConstraint1ISE_ || relConstraint1ISE<=settings_.minRelConstraint1ISE_;
		isLearningRateStarZero  = learningRateStar_==0 && !isInitInternalControllerEmpty;
		isCostFunctionConverged = relCost<=settings_.minRelCostGSLQP_ || isLearningRateStarZero;
		isOptimizationConverged = isCostFunctionConverged==true && isConstraint1Satisfied==true;
		isInitInternalControllerEmpty = false;

	}  // end of while loop

	if (settings_.displayInfo_)  std::cerr << "\n#### Final rollout" << std::endl;

#ifdef BENCHMARK
	BENCHMARK_nIterationsFP_++;
	BENCHMARK_start_ = std::chrono::steady_clock::now();
#endif

	bool computeISEs = settings_.noStateConstraints_==false ||
			settings_.displayInfo_==true || settings_.displayShortSummary_==true;

	// finding the final optimal learningRate and getting the optimal trajectories and controller
	maxLearningRate_ = settings_.maxLearningRateGSLQP_;
	lineSearch(computeISEs);

#ifdef BENCHMARK
	BENCHMARK_end_ = std::chrono::steady_clock::now();
	BENCHMARK_diff_ = BENCHMARK_end_ - BENCHMARK_start_;
	BENCHMARK_tAvgFP_ = ((1.0 - 1.0/BENCHMARK_nIterationsFP_)* BENCHMARK_tAvgFP_) +
			(1.0/BENCHMARK_nIterationsFP_)*std::chrono::duration_cast<std::chrono::milliseconds>(BENCHMARK_diff_).count();
#endif

	/*
	 * adds the deleted controller parts
	 */
	runExit();

	// display
	if (settings_.displayInfo_  || settings_.displayShortSummary_)  {

		std::cerr << "\n++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
		std::cerr <<   "++++++++++++++++ SLQ solver is terminated ++++++++++++++++" << std::endl;
		std::cerr <<   "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
		std::cerr << "Number of Iterations:      " <<  iteration_+1 << " out of " << settings_.maxNumIterationsSLQ_ << std::endl;

		printRolloutInfo();

		if (isOptimizationConverged==true) {
			if (isLearningRateStarZero==true)
				std::cerr << "SLQ successfully terminates as learningRate reduced to zero." << std::endl;
			else
				std::cerr << "SLQ successfully terminates as cost relative change (relCost=" << relCost <<") reached to the minimum value." << std::endl;

			if (nominalConstraint1ISE_<=settings_.minAbsConstraint1ISE_)
				std::cerr << "Type-1 constraint absolute ISE (absConstraint1ISE=" << nominalConstraint1ISE_ << ") reached to the minimum value." << std::endl;
			else
				std::cerr << "Type-1 constraint relative ISE (relConstraint1ISE=" << relConstraint1ISE << ") reached to the minimum value." << std::endl;
		} else {
			std::cerr << "Maximum number of iterations has reached." << std::endl;
		}
		std::cerr << std::endl;
	}

}

}  // ocs2 namespace



