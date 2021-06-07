#include <cmath>
#include <vector>

#include "modelSpec.h"

#include "parameters.h"

class EulerLIF : public NeuronModels::Base
{
public:
    DECLARE_MODEL(EulerLIF, 6, 2);

    SET_SIM_CODE(
        "if ($(RefracTime) <= 0.0) {\n"
        "  $(V) += (DT / $(TauM))*(($(Vrest) - $(V)) + $(Ioffset)) + $(Isyn);\n"
        "}\n"
        "else {\n"
        "  $(RefracTime) -= DT;\n"
        "}\n");

    SET_THRESHOLD_CONDITION_CODE("$(RefracTime) <= 0.0 && $(V) >= $(Vthresh)");

    SET_RESET_CODE(
        "$(V) = $(Vreset);\n"
        "$(RefracTime) = $(TauRefrac);\n");

    SET_PARAM_NAMES({
        "TauM",       // Membrane time constant [ms]
        "Vrest",      // Resting membrane potential [mV]
        "Vreset",     // Reset voltage [mV]
        "Vthresh",    // Spiking threshold [mV]
        "Ioffset",    // Offset current
        "TauRefrac"});

    SET_VARS({{"V", "scalar"}, {"RefracTime", "scalar"}});
};
IMPLEMENT_MODEL(EulerLIF);

void modelDefinition(NNmodel &model)
{
    // Use approximate exponentials etc to speed up plasticity
    GENN_PREFERENCES.optimizeCode = true;

    model.setDT(Parameters::timestep);
    model.setName("synth");
    model.setDefaultVarLocation(VarLocation::DEVICE);
    model.setDefaultSparseConnectivityLocation(VarLocation::DEVICE);
    model.setTiming(true);
    model.setMergePostsynapticModels(true);
    model.setSeed(1234);

    //---------------------------------------------------------------------------
    // Build model
    //---------------------------------------------------------------------------
    InitVarSnippet::Uniform::ParamValues vDist(
        Parameters::resetVoltage,       // 0 - min
        Parameters::thresholdVoltage);  // 1 - max

    InitSparseConnectivitySnippet::FixedProbability::ParamValues fixedProb(
        Parameters::probabilityConnection); // 0 - prob

    // LIF model parameters
    EulerLIF::ParamValues lifParams(
        20.0,                           // 0 - TauM
        Parameters::resetVoltage,       // 1 - Vrest
        Parameters::resetVoltage,       // 2 - Vreset
        Parameters::thresholdVoltage,   // 3 - Vthresh
        0.0,                            // 4 - Ioffset
        2.0);                           // 5 - TauRefrac

    // LIF initial conditions
    EulerLIF::VarValues lifInit(
        0.0,    // 0 - V
        0.0);   // 1 - RefracTime

    // Static synapse parameters
    WeightUpdateModels::StaticPulse::VarValues excitatoryStaticSynapseInit(
        Parameters::excitatoryWeight);    // 0 - Wij (mV)

    NeuronModels::PoissonNew::ParamValues poissonParams(Parameters::inputRate); // 0 - rate (Hz)
    NeuronModels::PoissonNew::VarValues poissonInit(0.0);                       // 0 - timeStepToSpike

    // Create IF_curr neuron
    auto *poisson = model.addNeuronPopulation<NeuronModels::PoissonNew>("P", Parameters::numNeurons,
                                                                        poissonParams, poissonInit);

    // Enable spike recording
	//poisson->setSpikeRecordingEnabled(true);

    model.addSynapsePopulation<WeightUpdateModels::StaticPulse, PostsynapticModels::DeltaCurr>(
        "PP", SynapseMatrixType::PROCEDURAL_GLOBALG, Parameters::delayTimesteps,
        "P", "P",
        {}, excitatoryStaticSynapseInit,
        {}, {},
        initConnectivity<InitSparseConnectivitySnippet::FixedProbability>(fixedProb));
}