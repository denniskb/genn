#include "code_generator/generateInit.h"

// Standard C++ includes
#include <string>

// GeNN includes
#include "models.h"

// GeNN code generator includes
#include "code_generator/codeStream.h"
#include "code_generator/modelSpecMerged.h"
#include "code_generator/substitutions.h"

using namespace CodeGenerator;

//--------------------------------------------------------------------------
// Anonymous namespace
//--------------------------------------------------------------------------
namespace
{
void genInitSpikeCount(CodeStream &os, const BackendBase &backend, const Substitutions &popSubs, 
                       const NeuronInitGroupMerged &ng, bool spikeEvent)
{
    // Is initialisation required at all
    const bool initRequired = spikeEvent ? ng.getArchetype().isSpikeEventRequired() : true;
    if(initRequired) {
        // Generate variable initialisation code
        backend.genPopVariableInit(os, popSubs,
            [&ng, spikeEvent] (CodeStream &os, Substitutions &)
            {
                // Get variable name
                const char *spikeCntName = spikeEvent ? "spkCntEvnt" : "spkCnt";

                // Is delay required
                const bool delayRequired = spikeEvent ?
                    ng.getArchetype().isDelayRequired() :
                    (ng.getArchetype().isTrueSpikeRequired() && ng.getArchetype().isDelayRequired());

                if(delayRequired) {
                    os << "for (unsigned int d = 0; d < " << ng.getArchetype().getNumDelaySlots() << "; d++)";
                    {
                        CodeStream::Scope b(os);
                        os << "group->" << spikeCntName << "[d] = 0;" << std::endl;
                    }
                }
                else {
                    os << "group->" << spikeCntName << "[0] = 0;" << std::endl;
                }
            });
    }

}
//--------------------------------------------------------------------------
void genInitSpikes(CodeStream &os, const BackendBase &backend, const Substitutions &popSubs, 
                   const NeuronInitGroupMerged &ng, bool spikeEvent)
{
    // Is initialisation required at all
    const bool initRequired = spikeEvent ? ng.getArchetype().isSpikeEventRequired() : true;
    if(initRequired) {
        // Generate variable initialisation code
        backend.genVariableInit(os, "group->numNeurons", "id", popSubs,
            [&ng, spikeEvent] (CodeStream &os, Substitutions &varSubs)
            {
                // Get variable name
                const char *spikeName = spikeEvent ? "spkEvnt" : "spk";

                // Is delay required
                const bool delayRequired = spikeEvent ?
                    ng.getArchetype().isDelayRequired() :
                    (ng.getArchetype().isTrueSpikeRequired() && ng.getArchetype().isDelayRequired());

                if(delayRequired) {
                    os << "for (unsigned int d = 0; d < " << ng.getArchetype().getNumDelaySlots() << "; d++)";
                    {
                        CodeStream::Scope b(os);
                        os << "group->" << spikeName << "[(d * group->numNeurons) + " + varSubs["id"] + "] = 0;" << std::endl;
                    }
                }
                else {
                    os << "group->" << spikeName << "[" << varSubs["id"] << "] = 0;" << std::endl;
                }
            });
    }
}
//------------------------------------------------------------------------
void genInitSpikeTime(CodeStream &os, const BackendBase &backend, const Substitutions &popSubs,
                      const NeuronInitGroupMerged &ng, const std::string &varName)
{
    // Generate variable initialisation code
    backend.genVariableInit(os, "group->numNeurons", "id", popSubs,
        [varName, &ng] (CodeStream &os, Substitutions &varSubs)
        {
            // Is delay required
            if(ng.getArchetype().isDelayRequired()) {
                os << "for (unsigned int d = 0; d < " << ng.getArchetype().getNumDelaySlots() << "; d++)";
                {
                    CodeStream::Scope b(os);
                    os << "group->" << varName << "[(d * group->numNeurons) + " + varSubs["id"] + "] = -TIME_MAX;" << std::endl;
                }
            }
            else {
                os << "group->" << varName << "[" << varSubs["id"] << "] = -TIME_MAX;" << std::endl;
            }
        });
}
//------------------------------------------------------------------------
template<typename I, typename Q, typename P, typename D>
void genInitNeuronVarCode(CodeStream &os, const BackendBase &backend, const Substitutions &popSubs,
                          const Models::Base::VarVec &vars, const std::string &fieldSuffix, const std::string &countMember, 
                          size_t numDelaySlots, const size_t groupIndex, const std::string &ftype,
                          I getVarInitialiser, Q isVarQueueRequired, P isParamHeterogeneousFn, D isDerivedParamHeterogeneousFn)
{
    const std::string count = "group->" + countMember;
    for (size_t k = 0; k < vars.size(); k++) {
        const auto &varInit = getVarInitialiser(k);

        // If this variable has any initialisation code
        if(!varInit.getSnippet()->getCode().empty()) {
            CodeStream::Scope b(os);

            // Generate target-specific code to initialise variable
            backend.genVariableInit(os, count, "id", popSubs,
                [&vars, &varInit, &fieldSuffix, &ftype, groupIndex, k, count, isVarQueueRequired, isParamHeterogeneousFn, isDerivedParamHeterogeneousFn, numDelaySlots]
                (CodeStream &os, Substitutions &varSubs)
                {
                    // Substitute in parameters and derived parameters for initialising variables
                    varSubs.addParamValueSubstitution(varInit.getSnippet()->getParamNames(), varInit.getParams(),
                                                      [k, isParamHeterogeneousFn](size_t p) { return isParamHeterogeneousFn(k, p); },
                                                      "", "group->", vars[k].name + fieldSuffix);
                    varSubs.addVarValueSubstitution(varInit.getSnippet()->getDerivedParams(), varInit.getDerivedParams(),
                                                    [k, isDerivedParamHeterogeneousFn](size_t p) { return isDerivedParamHeterogeneousFn(k, p); },
                                                    "", "group->", vars[k].name + fieldSuffix);
                    varSubs.addVarNameSubstitution(varInit.getSnippet()->getExtraGlobalParams(),
                                                   "", "group->", vars[k].name + fieldSuffix);

                    // If variable requires a queue
                    if (isVarQueueRequired(k)) {
                        // Generate initial value into temporary variable
                        os << vars[k].type << " initVal;" << std::endl;
                        varSubs.addVarSubstitution("value", "initVal");


                        std::string code = varInit.getSnippet()->getCode();
                        varSubs.applyCheckUnreplaced(code, "initVar : " + vars[k].name + "merged" + std::to_string(groupIndex));
                        code = ensureFtype(code, ftype);
                        os << code << std::endl;

                        // Copy this into all delay slots
                        os << "for (unsigned int d = 0; d < " << numDelaySlots << "; d++)";
                        {
                            CodeStream::Scope b(os);
                            os << "group->" + vars[k].name << fieldSuffix << "[(d * " << count << ") + " + varSubs["id"] + "] = initVal;" << std::endl;
                        }
                    }
                    else {
                        varSubs.addVarSubstitution("value", "group->" + vars[k].name + fieldSuffix + "[" + varSubs["id"] + "]");

                        std::string code = varInit.getSnippet()->getCode();
                        varSubs.applyCheckUnreplaced(code, "initVar : " + vars[k].name + "merged" + std::to_string(groupIndex));
                        code = ensureFtype(code, ftype);
                        os << code << std::endl;
                    }
                });
        }
    }
}
//------------------------------------------------------------------------
template<typename I, typename P, typename D>
void genInitNeuronVarCode(CodeStream &os, const BackendBase &backend, const Substitutions &popSubs,
                          const Models::Base::VarVec &vars, const std::string &fieldSuffix, const std::string &countMember, 
                          const size_t groupIndex, const std::string &ftype, 
                          I getVarInitialiser, P isParamHeterogeneousFn, D isDerivedParamHeterogeneousFn)
{
    genInitNeuronVarCode(os, backend, popSubs, vars, fieldSuffix, countMember, 0, groupIndex, ftype,
                         getVarInitialiser,
                         [](size_t){ return false; }, 
                         isParamHeterogeneousFn,
                         isDerivedParamHeterogeneousFn);
}
//------------------------------------------------------------------------
// Initialise one row of weight update model variables
void genInitWUVarCode(CodeStream &os, const BackendBase &backend, const Substitutions &popSubs, 
                      const SynapseGroupMergedBase &sg, const std::string &ftype)
{
    const auto vars = sg.getArchetype().getWUModel()->getVars();
    for (size_t k = 0; k < vars.size(); k++) {
        const auto &varInit = sg.getArchetype().getWUVarInitialisers().at(k);

        // If this variable has any initialisation code and doesn't require a kernel
        if(!varInit.getSnippet()->getCode().empty() && !varInit.getSnippet()->requiresKernel()) {
            CodeStream::Scope b(os);

            // Generate target-specific code to initialise variable
            backend.genSynapseVariableRowInit(os, sg, popSubs,
                [&vars, &varInit, &sg, &ftype, k](CodeStream &os, Substitutions &varSubs)
                {
                    varSubs.addVarSubstitution("value", "group->" + vars[k].name + "[" + varSubs["id_syn"] +  "]");
                    varSubs.addParamValueSubstitution(varInit.getSnippet()->getParamNames(), varInit.getParams(),
                                                      [k, &sg](size_t p) { return sg.isWUVarInitParamHeterogeneous(k, p); },
                                                      "", "group->", vars[k].name);
                    varSubs.addVarValueSubstitution(varInit.getSnippet()->getDerivedParams(), varInit.getDerivedParams(),
                                                      [k, &sg](size_t p) { return sg.isWUVarInitDerivedParamHeterogeneous(k, p); },
                                                      "", "group->", vars[k].name);
                    varSubs.addVarNameSubstitution(varInit.getSnippet()->getExtraGlobalParams(),
                                                   "", "group->", vars[k].name);

                    std::string code = varInit.getSnippet()->getCode();
                    varSubs.applyCheckUnreplaced(code, "initVar : merged" + vars[k].name + std::to_string(sg.getIndex()));
                    code = ensureFtype(code, ftype);
                    os << code << std::endl;
                });
        }
    }
}
//------------------------------------------------------------------------
// Generate either row or column connectivity init code
void genInitConnectivity(CodeStream &os, Substitutions &popSubs, const SynapseConnectivityInitGroupMerged &sg,
                         const std::string &ftype, bool rowNotColumns)
{
    const auto &connectInit = sg.getArchetype().getConnectivityInitialiser();
    const auto *snippet = connectInit.getSnippet();

    // Add substitutions
    popSubs.addFuncSubstitution(rowNotColumns ? "endRow" : "endCol", 0, "break");
    popSubs.addParamValueSubstitution(snippet->getParamNames(), connectInit.getParams(),
                                      [&sg](size_t i) { return sg.isConnectivityInitParamHeterogeneous(i);  },
                                      "", "group->");
    popSubs.addVarValueSubstitution(snippet->getDerivedParams(), connectInit.getDerivedParams(),
                                    [&sg](size_t i) { return sg.isConnectivityInitDerivedParamHeterogeneous(i);  },
                                    "", "group->");
    popSubs.addVarNameSubstitution(snippet->getExtraGlobalParams(), "", "group->");

    // Initialise state variables and loop on generated code to initialise sparse connectivity
    os << "// Build sparse connectivity" << std::endl;
    const auto stateVars = rowNotColumns ? snippet->getRowBuildStateVars() : snippet->getColBuildStateVars();
    for(const auto &a : stateVars) {
        // Apply substitutions to value
        std::string value = a.value;
        popSubs.applyCheckUnreplaced(value, "initSparseConnectivity state var : merged" + std::to_string(sg.getIndex()));

        os << a.type << " " << a.name << " = " << value << ";" << std::endl;
    }
    os << "while(true)";
    {
        CodeStream::Scope b(os);

        // Apply substitutions to row build code
        std::string code = rowNotColumns ? snippet->getRowBuildCode() : snippet->getColBuildCode();
        popSubs.addVarNameSubstitution(stateVars);
        popSubs.applyCheckUnreplaced(code, "initSparseConnectivity : merged" + std::to_string(sg.getIndex()));
        code = ensureFtype(code, ftype);

        // Write out code
        os << code << std::endl;
    }
}
}   // Anonymous namespace

//--------------------------------------------------------------------------
// CodeGenerator
//--------------------------------------------------------------------------
void CodeGenerator::generateInit(CodeStream &os, BackendBase::MemorySpaces &memorySpaces,
                                 const ModelSpecMerged &modelMerged, const BackendBase &backend)
{
    os << "#include \"definitionsInternal.h\"" << std::endl;

    // Generate functions to push merged synapse group structures
    const ModelSpecInternal &model = modelMerged.getModel();

    backend.genInit(os, modelMerged, memorySpaces,
        // Preamble handler
        [&modelMerged, &backend](CodeStream &os)
        {
            modelMerged.genMergedGroupPush(os, modelMerged.getMergedNeuronInitGroups(), backend);
            modelMerged.genMergedGroupPush(os, modelMerged.getMergedSynapseDenseInitGroups(), backend);
            modelMerged.genMergedGroupPush(os, modelMerged.getMergedSynapseConnectivityInitGroups(), backend);
            modelMerged.genMergedGroupPush(os, modelMerged.getMergedSynapseSparseInitGroups(), backend);
        },
        // Local neuron group initialisation
        [&backend, &model](CodeStream &os, const NeuronInitGroupMerged &ng, Substitutions &popSubs)
        {
            // Initialise spike counts
            genInitSpikeCount(os, backend, popSubs, ng, false);
            genInitSpikeCount(os, backend, popSubs, ng, true);

            // Initialise spikes
            genInitSpikes(os, backend, popSubs, ng, false);
            genInitSpikes(os, backend, popSubs, ng, true);

            // Initialize spike times
            if(ng.getArchetype().isSpikeTimeRequired()) {
                genInitSpikeTime(os, backend, popSubs, ng, "sT");
            }

            // Initialize previous spike times
            if(ng.getArchetype().isPrevSpikeTimeRequired()) {
                genInitSpikeTime(os, backend, popSubs, ng, "prevST");
            }
               
            // Initialize spike-like-event times
            if(ng.getArchetype().isSpikeEventTimeRequired()) {
                genInitSpikeTime(os, backend, popSubs, ng, "seT");
            }

            // Initialize previous spike-like-event times
            if(ng.getArchetype().isPrevSpikeEventTimeRequired()) {
                genInitSpikeTime(os, backend, popSubs, ng, "prevSET");
            }
       
            // If neuron group requires delays, zero spike queue pointer
            if(ng.getArchetype().isDelayRequired()) {
                backend.genPopVariableInit(os, popSubs,
                    [](CodeStream &os, Substitutions &)
                    {
                        os << "*group->spkQuePtr = 0;" << std::endl;
                    });
            }

            // Initialise neuron variables
            genInitNeuronVarCode(os, backend, popSubs, ng.getArchetype().getNeuronModel()->getVars(), "", "numNeurons",
                                 ng.getArchetype().getNumDelaySlots(), ng.getIndex(), model.getPrecision(),
                                 [&ng](size_t i){ return ng.getArchetype().getVarInitialisers().at(i); },
                                 [&ng](size_t i){ return ng.getArchetype().isVarQueueRequired(i); },
                                 [&ng](size_t v, size_t p) { return ng.isVarInitParamHeterogeneous(v, p); },
                                 [&ng](size_t v, size_t p) { return ng.isVarInitDerivedParamHeterogeneous(v, p); });

            // Loop through incoming synaptic populations
            for(size_t i = 0; i < ng.getArchetype().getMergedInSyn().size(); i++) {
                CodeStream::Scope b(os);

                const auto *sg = ng.getArchetype().getMergedInSyn()[i];

                // If this synapse group's input variable should be initialised on device
                // Generate target-specific code to initialise variable
                backend.genVariableInit(os, "group->numNeurons", "id", popSubs,
                    [&model, i] (CodeStream &os, Substitutions &varSubs)
                    {
                        os << "group->inSynInSyn" << i << "[" << varSubs["id"] << "] = " << model.scalarExpr(0.0) << ";" << std::endl;
                    });

                // If dendritic delays are required
                if(sg->isDendriticDelayRequired()) {
                    backend.genVariableInit(os, "group->numNeurons", "id", popSubs,
                        [&model, sg, i](CodeStream &os, Substitutions &varSubs)
                        {
                            os << "for (unsigned int d = 0; d < " << sg->getMaxDendriticDelayTimesteps() << "; d++)";
                            {
                                CodeStream::Scope b(os);
                                const std::string denDelayIndex = "(d * group->numNeurons) + " + varSubs["id"];
                                os << "group->denDelayInSyn" << i << "[" << denDelayIndex << "] = " << model.scalarExpr(0.0) << ";" << std::endl;
                            }
                        });

                    // Zero dendritic delay pointer
                    backend.genPopVariableInit(os, popSubs,
                        [i](CodeStream &os, Substitutions &)
                        {
                            os << "*group->denDelayPtrInSyn" << i << " = 0;" << std::endl;
                        });
                }

                // If postsynaptic model variables should be individual
                if(sg->getMatrixType() & SynapseMatrixWeight::INDIVIDUAL_PSM) {
                    genInitNeuronVarCode(os, backend, popSubs, sg->getPSModel()->getVars(), 
                                         "InSyn" + std::to_string(i), "numNeurons",
                                         i, model.getPrecision(),
                                         [sg](size_t i){ return sg->getPSVarInitialisers().at(i); },
                                         [&ng, i](size_t v, size_t p) { return ng.isPSMVarInitParamHeterogeneous(i, v, p); },
                                         [&ng, i](size_t v, size_t p) { return ng.isPSMVarInitDerivedParamHeterogeneous(i, v, p); });
                }
            }

            // Loop through incoming synaptic populations with postsynaptic variables
            // **NOTE** number of delay slots is based on the target neuron (for simplicity) but whether delay is required is based on the synapse group
            const auto inSynWithPostVars = ng.getArchetype().getInSynWithPostVars();
            for(size_t i = 0; i < inSynWithPostVars.size(); i++) {
                const auto *sg = inSynWithPostVars.at(i);
                genInitNeuronVarCode(os, backend, popSubs, sg->getWUModel()->getPostVars(),
                                     "WUPost" + std::to_string(i), "numNeurons", sg->getTrgNeuronGroup()->getNumDelaySlots(),
                                     i, model.getPrecision(),
                                     [&sg](size_t i){ return sg->getWUPostVarInitialisers().at(i); },
                                     [&sg](size_t){ return (sg->getBackPropDelaySteps() != NO_DELAY); },
                                     [&ng, i](size_t v, size_t p) { return ng.isInSynWUMVarInitParamHeterogeneous(i, v, p); },
                                     [&ng, i](size_t v, size_t p) { return ng.isInSynWUMVarInitDerivedParamHeterogeneous(i, v, p); });
            }

            // Loop through outgoing synaptic populations with presynaptic variables
            // **NOTE** number of delay slots is based on the source neuron (for simplicity) but whether delay is required is based on the synapse group
            const auto outSynWithPostVars = ng.getArchetype().getOutSynWithPreVars();
            for(size_t i = 0; i < outSynWithPostVars.size(); i++) {
                const auto *sg = outSynWithPostVars.at(i);
                genInitNeuronVarCode(os, backend, popSubs, sg->getWUModel()->getPreVars(),
                                     "WUPre" + std::to_string(i), "numNeurons", sg->getSrcNeuronGroup()->getNumDelaySlots(),
                                     i, model.getPrecision(),
                                     [&sg](size_t i){ return sg->getWUPreVarInitialisers().at(i); },
                                     [&sg](size_t){ return (sg->getDelaySteps() != NO_DELAY); },
                                     [&ng, i](size_t v, size_t p) { return ng.isOutSynWUMVarInitParamHeterogeneous(i, v, p); },
                                     [&ng, i](size_t v, size_t p) { return ng.isOutSynWUMVarInitDerivedParamHeterogeneous(i, v, p); });
            }

            // Loop through current sources
            os << "// current source variables" << std::endl;
            for(size_t i = 0; i < ng.getArchetype().getCurrentSources().size(); i++) {
                const auto *cs = ng.getArchetype().getCurrentSources()[i];

                genInitNeuronVarCode(os, backend, popSubs, cs->getCurrentSourceModel()->getVars(), 
                                     "CS" + std::to_string(i), "numNeurons",
                                     i, model.getPrecision(),
                                     [cs](size_t i){ return cs->getVarInitialisers().at(i); },
                                     [&ng, i](size_t v, size_t p) { return ng.isCurrentSourceVarInitParamHeterogeneous(i, v, p); },
                                     [&ng, i](size_t v, size_t p) { return ng.isCurrentSourceVarInitDerivedParamHeterogeneous(i, v, p); });
            }
        },
        // Dense syanptic matrix variable initialisation
        [&backend, &model](CodeStream &os, const SynapseDenseInitGroupMerged &sg, Substitutions &popSubs)
        {
            // Loop through rows
            os << "for(unsigned int i = 0; i < group->numSrcNeurons; i++)";
            {
                CodeStream::Scope b(os);
                popSubs.addVarSubstitution("id_pre", "i");
                genInitWUVarCode(os, backend, popSubs, sg, model.getPrecision());

            }
        },
        // Sparse synaptic matrix row connectivity initialisation
        [&model](CodeStream &os, const SynapseConnectivityInitGroupMerged &sg, Substitutions &popSubs)
        {
            genInitConnectivity(os, popSubs, sg, model.getPrecision(), true);
        },
        // Sparse synaptic matrix column connectivity initialisation
        [&model](CodeStream &os, const SynapseConnectivityInitGroupMerged &sg, Substitutions &popSubs)
        {
            genInitConnectivity(os, popSubs, sg, model.getPrecision(), false);
        },
        // Kernel matrix var initialisation
        [&backend, &model](CodeStream &os, const SynapseConnectivityInitGroupMerged &sg, Substitutions &popSubs)
        {
            // Generate kernel index and add to substitutions
            os << "const unsigned int kernelInd = ";
            genKernelIndex(os, popSubs, sg);
            os << ";" << std::endl;
            popSubs.addVarSubstitution("id_kernel", "kernelInd");

            const auto vars = sg.getArchetype().getWUModel()->getVars();
            for(size_t k = 0; k < vars.size(); k++) {
                const auto &varInit = sg.getArchetype().getWUVarInitialisers().at(k);

                // If this variable require a kernel
                if(varInit.getSnippet()->requiresKernel()) {
                    CodeStream::Scope b(os);

                    popSubs.addVarSubstitution("value", "group->" + vars[k].name + "[" + popSubs["id_syn"] + "]");
                    popSubs.addParamValueSubstitution(varInit.getSnippet()->getParamNames(), varInit.getParams(),
                                                      [k, &sg](size_t p) { return sg.isWUVarInitParamHeterogeneous(k, p); },
                                                      "", "group->", vars[k].name);
                    popSubs.addVarValueSubstitution(varInit.getSnippet()->getDerivedParams(), varInit.getDerivedParams(),
                                                    [k, &sg](size_t p) { return sg.isWUVarInitDerivedParamHeterogeneous(k, p); },
                                                    "", "group->", vars[k].name);
                    popSubs.addVarNameSubstitution(varInit.getSnippet()->getExtraGlobalParams(),
                                                    "", "group->", vars[k].name);

                    std::string code = varInit.getSnippet()->getCode();
                    //popSubs.applyCheckUnreplaced(code, "initVar : merged" + vars[k].name + std::to_string(sg.getIndex()));
                    popSubs.apply(code);
                    code = ensureFtype(code, model.getPrecision());
                    os << code << std::endl;
                }
            }
        },
        // Sparse synaptic matrix var initialisation
        [&backend, &model](CodeStream &os, const SynapseSparseInitGroupMerged &sg, Substitutions &popSubs)
        {
            genInitWUVarCode(os, backend, popSubs, sg, model.getPrecision());
        },
        // Initialise push EGP handler
        [&backend, &modelMerged](CodeStream &os)
        {
            modelMerged.genScalarEGPPush(os, "NeuronInit", backend);
            modelMerged.genScalarEGPPush(os, "SynapseDenseInit", backend);
            modelMerged.genScalarEGPPush(os, "SynapseConnectivityInit", backend);
        },
        // Initialise sparse push EGP handler
        [&backend, &modelMerged](CodeStream &os)
        {
            modelMerged.genScalarEGPPush(os, "SynapseSparseInit", backend);
        });
}
