#include "code_generator/generateSynapseUpdate.h"

// Standard C++ includes
#include <string>

// GeNN includes
#include "groupMerged.h"
#include "modelSpecInternal.h"

// GeNN code generator includes
#include "code_generator/codeStream.h"
#include "code_generator/substitutions.h"
#include "code_generator/backendBase.h"
#include "code_generator/teeStream.h"

//--------------------------------------------------------------------------
// Anonymous namespace
//--------------------------------------------------------------------------
namespace
{
void applySynapseSubstitutions(CodeGenerator::CodeStream &os, std::string code, const std::string &errorContext, const SynapseGroupInternal &sg,
                               const CodeGenerator::Substitutions &baseSubs, const ModelSpecInternal &model, const CodeGenerator::BackendBase &backend)
{
    const auto *wu = sg.getWUModel();

    CodeGenerator::Substitutions synapseSubs(&baseSubs);

    // Substitute parameter and derived parameter names
    synapseSubs.addParamValueSubstitution(sg.getWUModel()->getParamNames(), sg.getWUParams());
    synapseSubs.addVarValueSubstitution(wu->getDerivedParams(), sg.getWUDerivedParams());
    synapseSubs.addVarNameSubstitution(wu->getExtraGlobalParams(), "", "(*synapseGroup.", ")");

    // Substitute names of pre and postsynaptic weight update variables
    const std::string delayedPreIdx = (sg.getDelaySteps() == NO_DELAY) ? synapseSubs["id_pre"] : "preReadDelayOffset + " + baseSubs["id_pre"];
    synapseSubs.addVarNameSubstitution(wu->getPreVars(), "", "synapseGroup.",
                                       "[" + delayedPreIdx + "]");

    const std::string delayedPostIdx = (sg.getBackPropDelaySteps() == NO_DELAY) ? synapseSubs["id_post"] : "postReadDelayOffset + " + baseSubs["id_post"];
    synapseSubs.addVarNameSubstitution(wu->getPostVars(), "", "synapseGroup.",
                                       "[" + delayedPostIdx + "]");

    // If weights are individual, substitute variables for values stored in global memory
    if (sg.getMatrixType() & SynapseMatrixWeight::INDIVIDUAL) {
        synapseSubs.addVarNameSubstitution(wu->getVars(), "", "synapseGroup.",
                                           "[" + synapseSubs["id_syn"] + "]");
    }
    // Otherwise, if weights are procedual
    else if (sg.getMatrixType() & SynapseMatrixWeight::PROCEDURAL) {
        const auto vars = wu->getVars();

        // Loop through variables and their initialisers
        auto var = vars.cbegin();
        auto varInit = sg.getWUVarInitialisers().cbegin();
        for (; var != vars.cend(); var++, varInit++) {
            // Configure variable substitutions
            CodeGenerator::Substitutions varSubs(&synapseSubs);
            varSubs.addVarSubstitution("value", "l" + var->name);
            varSubs.addParamValueSubstitution(varInit->getSnippet()->getParamNames(), varInit->getParams());
            varSubs.addVarValueSubstitution(varInit->getSnippet()->getDerivedParams(), varInit->getDerivedParams());

            // Generate variable initialization code
            std::string code = varInit->getSnippet()->getCode();
            varSubs.applyCheckUnreplaced(code, "initVar : " + var->name + sg.getName());

            // Declare local variable
            os << var->type << " " << "l" << var->name << ";" << std::endl;

            // Insert code to initialize variable into scope
            {
                CodeGenerator::CodeStream::Scope b(os);
                os << code << std::endl;;
            }
        }

        // Substitute variables for newly-declared local variables
        synapseSubs.addVarNameSubstitution(vars, "", "l");
    }
    // Otherwise, substitute variables for constant values
    else {
        synapseSubs.addVarValueSubstitution(wu->getVars(), sg.getWUConstInitVals());
    }

    neuronSubstitutionsInSynapticCode(synapseSubs, sg, synapseSubs["id_pre"],
                                      synapseSubs["id_post"], backend.getVarPrefix(),
                                      model.getDT());

    synapseSubs.apply(code);
    //synapseSubs.applyCheckUnreplaced(code, errorContext + " : " + sg.getName());
    code = CodeGenerator::ensureFtype(code, model.getPrecision());
    os << code;
}
}   // Anonymous namespace

//--------------------------------------------------------------------------
// CodeGenerator
//--------------------------------------------------------------------------
void CodeGenerator::generateSynapseUpdate(CodeStream &os, const ModelSpecInternal &model, const BackendBase &backend,
                                          bool standaloneModules)
{
    if(standaloneModules) {
        os << "#include \"runner.cc\"" << std::endl;
    }
    else {
        os << "#include \"definitionsInternal.h\"" << std::endl;
    }
    os << "#include \"supportCode.h\"" << std::endl;
    os << std::endl;

    // Loop through merged neuron groups
    std::stringstream mergedGroupArrayStream;
    std::stringstream mergedGroupFuncStream;
    CodeStream mergedGroupArray(mergedGroupArrayStream);
    CodeStream mergedGroupFunc(mergedGroupFuncStream);
    TeeStream mergedGroupStreams(mergedGroupArray, mergedGroupFunc);
    for(const auto &sg : model.getMergedLocalSynapseGroups()) {
        // Declare static array to hold merged synapse groups
        const size_t idx = sg.getIndex();
        const size_t numGroups = sg.getGroups().size();

        mergedGroupArray << "__device__ __constant__ MergedSynapseGroup" << idx << " dd_mergedSynapseGroup" << idx << "[" << numGroups << "];" << std::endl;

        // Write function to update
        mergedGroupFunc << "void pushMergedSynapseGroup" << idx << "ToDevice(const MergedSynapseGroup" << idx << " *group)";
        {
            CodeStream::Scope b(mergedGroupFunc);
            mergedGroupFunc << "CHECK_CUDA_ERRORS(cudaMemcpyToSymbol(dd_mergedSynapseGroup" << idx << ", group, " << numGroups << " * sizeof(MergedSynapseGroup" << idx << ")));" << std::endl;
        }
    }

    os << "// ------------------------------------------------------------------------" << std::endl;
    os << "// merged neuron group arrays" << std::endl;
    os << "// ------------------------------------------------------------------------" << std::endl;
    os << mergedGroupArrayStream.str();
    os << std::endl;

    os << "// ------------------------------------------------------------------------" << std::endl;
    os << "// merged neuron group functions" << std::endl;
    os << "// ------------------------------------------------------------------------" << std::endl;
    os << mergedGroupFuncStream.str();
    os << std::endl;

    // Synaptic update kernels
    backend.genSynapseUpdate(os, model,
        // Presynaptic weight update threshold
        [&backend, &model](CodeStream &os, const SynapseGroupMerged &sg, Substitutions &baseSubs)
        {
            Substitutions synapseSubs(&baseSubs);

            // Make weight update model substitutions
            synapseSubs.addParamValueSubstitution(sg.getArchetype().getWUModel()->getParamNames(), sg.getArchetype().getWUParams());
            synapseSubs.addVarValueSubstitution(sg.getArchetype().getWUModel()->getDerivedParams(), sg.getArchetype().getWUDerivedParams());
            synapseSubs.addVarNameSubstitution(sg.getArchetype().getWUModel()->getExtraGlobalParams(), "", "(*synapseGroup.", ")");

            // Get read offset if required
            //const std::string offset = sg.getSrcNeuronGroup()->isDelayRequired() ? "preReadDelayOffset + " : "";
            //preNeuronSubstitutionsInSynapticCode(synapseSubs, sg, offset, "", baseSubs["id_pre"], backend.getVarPrefix());

            // Get event threshold condition code
            std::string code = sg.getArchetype().getWUModel()->getEventThresholdConditionCode();
            synapseSubs.applyCheckUnreplaced(code, "eventThresholdConditionCode");
            code = ensureFtype(code, model.getPrecision());
            os << code;
        },
        // Presynaptic spike
        [&backend, &model](CodeStream &os, const SynapseGroupMerged &sg, Substitutions &baseSubs)
        {
            applySynapseSubstitutions(os, sg.getArchetype().getWUModel()->getSimCode(), "simCode",
                                      sg.getArchetype(), baseSubs, model, backend);
        },
        // Presynaptic spike-like event
        [&backend, &model](CodeStream &os, const SynapseGroupMerged &sg, Substitutions &baseSubs)
        {
            applySynapseSubstitutions(os, sg.getArchetype().getWUModel()->getEventCode(), "eventCode",
                                      sg.getArchetype(), baseSubs, model, backend);
        },
        // Procedural connectivity
        [&backend, &model](CodeStream &os, const SynapseGroupMerged &sg, Substitutions &baseSubs)
        {
            baseSubs.addFuncSubstitution("endRow", 0, "break");

            // Initialise row building state variables for procedural connectivity
            const auto &connectInit = sg.getArchetype().getConnectivityInitialiser();
            for(const auto &a : connectInit.getSnippet()->getRowBuildStateVars()) {
                os << a.type << " " << a.name << " = " << a.value << ";" << std::endl;
            }

            // Loop through synapses in row
            os << "while(true)";
            {
                CodeStream::Scope b(os);
                Substitutions synSubs(&baseSubs);

                synSubs.addParamValueSubstitution(connectInit.getSnippet()->getParamNames(), connectInit.getParams());
                synSubs.addVarValueSubstitution(connectInit.getSnippet()->getDerivedParams(), connectInit.getDerivedParams());
                synSubs.addVarNameSubstitution(connectInit.getSnippet()->getExtraGlobalParams(), "", "(*synapseGroup.", ")");

                std::string pCode = connectInit.getSnippet()->getRowBuildCode();
                synSubs.applyCheckUnreplaced(pCode, "proceduralSparseConnectivity : merged " + sg.getIndex());
                pCode = ensureFtype(pCode, model.getPrecision());

                // Write out code
                os << pCode << std::endl;
            }
        },
        // Postsynaptic learning code
        [&backend, &model](CodeStream &os, const SynapseGroupMerged &sg, const Substitutions &baseSubs)
        {
            if (!sg.getArchetype().getWUModel()->getLearnPostSupportCode().empty()) {
                os << " using namespace " << sg.getArchetype().getName() << "_weightupdate_simLearnPost;" << std::endl;
            }

            applySynapseSubstitutions(os, sg.getArchetype().getWUModel()->getLearnPostCode(), "learnPostCode",
                                      sg.getArchetype(), baseSubs, model, backend);
        },
        // Synapse dynamics
        [&backend, &model](CodeStream &os, const SynapseGroupMerged &sg, const Substitutions &baseSubs)
        {
            if (!sg.getArchetype().getWUModel()->getSynapseDynamicsSuppportCode().empty()) {
                os << " using namespace " << sg.getArchetype().getName() << "_weightupdate_synapseDynamics;" << std::endl;
            }

            applySynapseSubstitutions(os, sg.getArchetype().getWUModel()->getSynapseDynamicsCode(), "synapseDynamics",
                                      sg.getArchetype(), baseSubs, model, backend);
        }
    );
}
