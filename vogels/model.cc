#include <cmath>
#include <vector>

#include "modelSpec.h"

#include "parameters.h"

class EulerLIF : public NeuronModels::Base
{
public:
    DECLARE_MODEL(EulerLIF, 7, 2);

    SET_SIM_CODE(
        "if ($(RefracTime) <= 0.0)\n"
        "{\n"
        "  scalar alpha = (($(Isyn)) * $(Rmembrane));\n"
        "  $(V) += (DT / $(TauM))*(($(Vrest) - $(V)) + alpha + $(Ioffset));\n"
        "}\n"
        "else\n"
        "{\n"
        "  $(RefracTime) -= DT;\n"
        "}\n"
    );

    SET_THRESHOLD_CONDITION_CODE("$(RefracTime) <= 0.0 && $(V) >= $(Vthresh)");

    SET_RESET_CODE(
        "$(V) = $(Vreset);\n"
        "$(RefracTime) = $(TauRefrac);\n");

    SET_PARAM_NAMES({
        "C",          // Membrane capacitance
        "TauM",       // Membrane time constant [ms]
        "Vrest",      // Resting membrane potential [mV]
        "Vreset",     // Reset voltage [mV]
        "Vthresh",    // Spiking threshold [mV]
        "Ioffset",    // Offset current
        "TauRefrac"});

    SET_DERIVED_PARAMS({
        {"ExpTC", [](const std::vector<double> &pars, double dt){ return std::exp(-dt / pars[1]); }},
        {"Rmembrane", [](const std::vector<double> &pars, double){ return  pars[1] / pars[0]; }}});

    SET_VARS({{"V", "scalar"}, {"RefracTime", "scalar"}});
};
IMPLEMENT_MODEL(EulerLIF);

void modelDefinition(NNmodel &model)
{
    // Use approximate exponentials etc to speed up plasticity
    GENN_PREFERENCES.optimizeCode = true;

    model.setDT(Parameters::timestep);
    model.setName("vogels");
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
        200.0e-9,    // 0 - C
        20.0,   // 1 - TauM
        Parameters::restVoltage,  // 2 - Vrest
        Parameters::resetVoltage,  // 3 - Vreset
        Parameters::thresholdVoltage,  // 4 - Vthresh
        20.0,    // 5 - Ioffset
        5.0);    // 6 - TauRefrac

    // LIF initial conditions
    EulerLIF::VarValues lifInit(
        Parameters::restVoltage, //initVar<InitVarSnippet::Uniform>(vDist),     // 0 - V
        0.0);   // 1 - RefracTime

    // Static synapse parameters
	WeightUpdateModels::StaticPulse::VarValues excs_ini(
          Parameters::excitatoryWeight // 0 - g: the synaptic conductance value
    );
    WeightUpdateModels::StaticPulse::VarValues inhibs_ini(
          Parameters::inhibitoryWeight // 0 - g: the synaptic conductance value
    );

    PostsynapticModels::ExpCond::ParamValues excitatorySyns(
            5.0,      // 0 - tau_S: decay time constant for S [ms]
            -0.0     // 1 - Erev: Reversal potential
    );
    PostsynapticModels::ExpCond::ParamValues inhibitorySyns(
            10.0,      // 0 - tau_S: decay time constant for S [ms]
            -80.0     // 1 - Erev: Reversal potential
    );

    // Create IF_curr neuron
    auto *e = model.addNeuronPopulation<EulerLIF>("E", Parameters::numExcitatory, lifParams, lifInit);
    auto *i = model.addNeuronPopulation<EulerLIF>("I", Parameters::numInhibitory, lifParams, lifInit);

    // Enable spike recording
    //e->setSpikeRecordingEnabled(true);
    //i->setSpikeRecordingEnabled(true);

    model.addSynapsePopulation<WeightUpdateModels::StaticPulse, PostsynapticModels::ExpCond>(
        "EE", SynapseMatrixType::SPARSE_GLOBALG, Parameters::delayTimesteps,
        "E", "E",
        {}, excs_ini,
        excitatorySyns, {},
		initConnectivity<InitSparseConnectivitySnippet::FixedProbability>(fixedProb));
    model.addSynapsePopulation<WeightUpdateModels::StaticPulse, PostsynapticModels::ExpCond>(
        "EI", SynapseMatrixType::SPARSE_GLOBALG, Parameters::delayTimesteps,
        "E", "I",
        {}, excs_ini,
        excitatorySyns, {},
		initConnectivity<InitSparseConnectivitySnippet::FixedProbability>(fixedProb));
	model.addSynapsePopulation<WeightUpdateModels::StaticPulse, PostsynapticModels::ExpCond>(
        "IE", SynapseMatrixType::SPARSE_GLOBALG, Parameters::delayTimesteps,
        "I", "E",
        {}, inhibs_ini,
        inhibitorySyns, {},
		initConnectivity<InitSparseConnectivitySnippet::FixedProbability>(fixedProb));
	model.addSynapsePopulation<WeightUpdateModels::StaticPulse, PostsynapticModels::ExpCond>(
        "II", SynapseMatrixType::SPARSE_GLOBALG, Parameters::delayTimesteps,
        "I", "I",
        {}, inhibs_ini,
        inhibitorySyns, {},
		initConnectivity<InitSparseConnectivitySnippet::FixedProbability>(fixedProb));
}