#include "code_generator/generateNeuronUpdate.h"

// Standard C++ includes
#include <iostream>
#include <string>

// PLOG includes
#include <plog/Log.h>

// GeNN includes
#include "models.h"
#include "modelSpecInternal.h"

// GeNN code generator includes
#include "code_generator/backendBase.h"
#include "code_generator/codeGenUtils.h"
#include "code_generator/codeStream.h"
#include "code_generator/substitutions.h"
#include "code_generator/teeStream.h"

//--------------------------------------------------------------------------
// Anonymous namespace
//--------------------------------------------------------------------------
namespace
{
void addNeuronModelSubstitutions(CodeGenerator::Substitutions &substitution, const NeuronGroupInternal &ng,
                                 const std::string &sourceSuffix = "", const std::string &destSuffix = "")
{
    const NeuronModels::Base *nm = ng.getNeuronModel();
    substitution.addVarNameSubstitution(nm->getVars(), sourceSuffix, "l", destSuffix);
    substitution.addParamValueSubstitution(nm->getParamNames(), ng.getParams());
    substitution.addVarValueSubstitution(nm->getDerivedParams(), ng.getDerivedParams());
    substitution.addVarNameSubstitution(nm->getExtraGlobalParams(), "", "neuronGroup.");
    substitution.addVarNameSubstitution(nm->getAdditionalInputVars());
}
//--------------------------------------------------------------------------
void addPostsynapticModelSubstitutions(CodeGenerator::Substitutions &substitution, const SynapseGroupInternal *sg)
{
    const auto *psm = sg->getPSModel();
    if (sg->getMatrixType() & SynapseMatrixWeight::INDIVIDUAL_PSM) {
        substitution.addVarNameSubstitution(psm->getVars(), "", "lps", sg->getName());
    }
    else {
        substitution.addVarValueSubstitution(psm->getVars(), sg->getPSConstInitVals());
    }
    substitution.addParamValueSubstitution(psm->getParamNames(), sg->getPSParams());

    // Create iterators to iterate over the names of the postsynaptic model's derived parameters
    substitution.addVarValueSubstitution(psm->getDerivedParams(), sg->getPSDerivedParams());
    substitution.addVarNameSubstitution(psm->getExtraGlobalParams(), "", "", sg->getName());
}
}   // Anonymous namespace

//--------------------------------------------------------------------------
// CodeGenerator
//--------------------------------------------------------------------------
void CodeGenerator::generateNeuronUpdate(CodeStream &os, const ModelSpecInternal &model, const BackendBase &backend,
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
    for(const auto &ng : model.getMergedLocalNeuronGroups()) {
        // Declare static array to hold merged neuron groups
        const size_t idx = ng.getIndex();
        const size_t numGroups = ng.getGroups().size();

        mergedGroupArray << "__device__ __constant__ MergedNeuronGroup" << idx << " dd_mergedNeuronGroup" << idx << "[" << numGroups << "];" << std::endl;

        // Write function to update
        mergedGroupFunc << "void pushMergedNeuronGroup" << idx << "ToDevice(const MergedNeuronGroup" << idx << " *group)";
        {
            CodeStream::Scope b(mergedGroupFunc);
            mergedGroupFunc << "CHECK_CUDA_ERRORS(cudaMemcpyToSymbol(dd_mergedNeuronGroup" << idx << ", group, " << numGroups << " * sizeof(MergedNeuronGroup" << idx << ")));" << std::endl;
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

    // Neuron update kernel
    backend.genNeuronUpdate(os, model,
        // Sim handler
        [&backend, &model](CodeStream &os, const NeuronGroupMerged &ng, Substitutions &popSubs,
                           BackendBase::NeuronGroupMergedHandler genEmitTrueSpike, 
                           BackendBase::NeuronGroupMergedHandler genEmitSpikeLikeEvent)
        {
            const NeuronModels::Base *nm = ng.getArchetype().getNeuronModel();

            // Generate code to copy neuron state into local variable
            for(const auto &v : nm->getVars()) {
                if(v.access == VarAccess::READ_ONLY) {
                    os << "const ";
                }
                os << v.type << " l" << v.name << " = ";
                os << "neuronGroup." << v.name << "[";
                if (ng.getArchetype().isVarQueueRequired(v.name) && ng.getArchetype().isDelayRequired()) {
                    os << "readDelayOffset + ";
                }
                os << popSubs["id"] << "];" << std::endl;
            }
    
            // Also read spike time into local variable
            if(ng.getArchetype().isSpikeTimeRequired()) {
                assert(false);
                /*os << model.getTimePrecision() << " lsT = " << backend.getVarPrefix() << "sT" << ng.getName() << "[";
                if (ng.isDelayRequired()) {
                    os << "readDelayOffset + ";
                }
                os << popSubs["id"] << "];" << std::endl;*/
            }
            os << std::endl;

            // If neuron model sim code references ISyn/* (could still be the case if there are no incoming synapses)
            // OR any incoming synapse groups have post synaptic models which reference $(inSyn), declare it*/
            if (nm->getSimCode().find("$(Isyn)") != std::string::npos ||
                std::any_of(ng.getArchetype().getMergedInSyn().cbegin(), ng.getArchetype().getMergedInSyn().cend(),
                            [](const std::pair<SynapseGroupInternal*, std::vector<SynapseGroupInternal*>> &p)
                            {
                                return (p.first->getPSModel()->getApplyInputCode().find("$(inSyn)") != std::string::npos
                                        || p.first->getPSModel()->getDecayCode().find("$(inSyn)") != std::string::npos);
                            }))
            {
                os << model.getPrecision() << " Isyn = 0;" << std::endl;
            }

            Substitutions neuronSubs(&popSubs);
            neuronSubs.addVarSubstitution("Isyn", "Isyn");
            neuronSubs.addVarSubstitution("sT", "lsT");
            addNeuronModelSubstitutions(neuronSubs, ng.getArchetype());

            // Initialise any additional input variables supported by neuron model
            for (const auto &a : nm->getAdditionalInputVars()) {
                os << a.type << " " << a.name<< " = " << a.value << ";" << std::endl;
            }

            // Loop through incoming synapse groups
            for(size_t i = 0; i < ng.getArchetype().getMergedInSyn().size(); i++) {
                CodeStream::Scope b(os);

                const auto *sg = ng.getArchetype().getMergedInSyn()[i].first;;
                const auto *psm = sg->getPSModel();

                os << "// pull inSyn values in a coalesced access" << std::endl;
                os << model.getPrecision() << " linSyn = neuronGroup.inSyn" << i << "[" << popSubs["id"] << "];" << std::endl;

                // If dendritic delay is required
                if (sg->isDendriticDelayRequired()) {
                    // Get reference to dendritic delay buffer input for this timestep
                    os << model.getPrecision() << " &denDelayFront" << i << " = ";
                    os << "neuronGroup.denDelay" << i << "[(*neuronGroup.denDelayPtr" << i << " * neuronGroup.numNeurons) + " << popSubs["id"] << "];" << std::endl;

                    // Add delayed input from buffer into inSyn
                    os << "linSyn += denDelayFront" << i << ";" << std::endl;

                    // Zero delay buffer slot
                    os << "denDelayFront" << i << " = " << model.scalarExpr(0.0) << ";" << std::endl;
                }

                // If synapse group has individual postsynaptic variables, also pull these in a coalesced access
                if (sg->getMatrixType() & SynapseMatrixWeight::INDIVIDUAL_PSM) {
                    // **TODO** base behaviour from Models::Base
                    for (const auto &v : psm->getVars()) {
                        if(v.access == VarAccess::READ_ONLY) {
                            os << "const ";
                        }
                        os << v.type << " lps" << v.name;
                        os << " = neuronGroup." << v.name << i << "[" << neuronSubs["id"] << "];" << std::endl;
                    }
                }

                Substitutions inSynSubs(&neuronSubs);
                inSynSubs.addVarSubstitution("inSyn", "linSyn");
                addPostsynapticModelSubstitutions(inSynSubs, sg);

                // Apply substitutions to current converter code
                std::string psCode = psm->getApplyInputCode();
                inSynSubs.applyCheckUnreplaced(psCode, "postSyntoCurrent : merged " + i);
                psCode = ensureFtype(psCode, model.getPrecision());

                // Apply substitutions to decay code
                std::string pdCode = psm->getDecayCode();
                inSynSubs.applyCheckUnreplaced(pdCode, "decayCode : merged " + i);
                pdCode = ensureFtype(pdCode, model.getPrecision());

                if (!psm->getSupportCode().empty()) {
                    os << CodeStream::OB(29) << " using namespace " << sg->getPSModelTargetName() << "_postsyn;" << std::endl;
                }

                os << psCode << std::endl;
                os << pdCode << std::endl;

                if (!psm->getSupportCode().empty()) {
                    os << CodeStream::CB(29) << " // namespace bracket closed" << std::endl;
                }

                // Write back linSyn
                os << "neuronGroup.inSyn"  << i << "[" << inSynSubs["id"] << "] = linSyn;" << std::endl;

                // Copy any non-readonly postsynaptic model variables back to global state variables dd_V etc
                for (const auto &v : psm->getVars()) {
                    if(v.access == VarAccess::READ_WRITE) {
                        os << "neuronGroup." << v.name << i << "[" << inSynSubs["id"] << "]" << " = lps" << v.name << ";" << std::endl;
                    }
                }
            }

            // Loop through all of neuron group's current sources
            /*for(const auto *cs : ng.getCurrentSources())
            {
                os << "// current source " << cs->getName() << std::endl;
                CodeStream::Scope b(os);

                const auto *csm = cs->getCurrentSourceModel();

                // Read current source variables into registers
                for(const auto &v : csm->getVars()) {
                    os << v.type << " lcs" << v.name << " = " << backend.getVarPrefix() << v.name << cs->getName() << "[" << popSubs["id"] << "];" << std::endl;
                }

                Substitutions currSourceSubs(&popSubs);
                currSourceSubs.addFuncSubstitution("injectCurrent", 1, "Isyn += $(0)");
                currSourceSubs.addVarNameSubstitution(csm->getVars(), "", "lcs");
                currSourceSubs.addParamValueSubstitution(csm->getParamNames(), cs->getParams());
                currSourceSubs.addVarValueSubstitution(csm->getDerivedParams(), cs->getDerivedParams());
                currSourceSubs.addVarNameSubstitution(csm->getExtraGlobalParams(), "", "", cs->getName());

                std::string iCode = csm->getInjectionCode();
                currSourceSubs.applyCheckUnreplaced(iCode, "injectionCode : " + cs->getName());
                iCode = ensureFtype(iCode, model.getPrecision());
                os << iCode << std::endl;

                // Write read/write variables back to global memory
                for(const auto &v : csm->getVars()) {
                    if(v.access == VarAccess::READ_WRITE) {
                        os << backend.getVarPrefix() << v.name << cs->getName() << "[" << currSourceSubs["id"] << "] = lcs" << v.name << ";" << std::endl;
                    }
                }
            }*/

            if (!nm->getSupportCode().empty()) {
                assert(false);
                //os << " using namespace " << ng.getName() << "_neuron;" << std::endl;
            }

            // If a threshold condition is provided
            std::string thCode = nm->getThresholdConditionCode();
            if (!thCode.empty()) {
                os << "// test whether spike condition was fulfilled previously" << std::endl;

                neuronSubs.applyCheckUnreplaced(thCode, "thresholdConditionCode : merged" + std::to_string(ng.getIndex()));
                thCode= ensureFtype(thCode, model.getPrecision());

                if (nm->isAutoRefractoryRequired()) {
                    os << "const bool oldSpike= (" << thCode << ");" << std::endl;
                }
            }
            // Otherwise, if any outgoing synapse groups have spike-processing code
            /*else if(std::any_of(ng.getOutSyn().cbegin(), ng.getOutSyn().cend(),
                                [](const SynapseGroupInternal *sg){ return !sg->getWUModel()->getSimCode().empty(); }))
            {
                LOGW << "No thresholdConditionCode for neuron type " << typeid(*nm).name() << " used for population \"" << ng.getName() << "\" was provided. There will be no spikes detected in this population!";
            }*/

            os << "// calculate membrane potential" << std::endl;
            std::string sCode = nm->getSimCode();
            neuronSubs.applyCheckUnreplaced(sCode, "simCode : merged" + std::to_string(ng.getIndex()));
            sCode = ensureFtype(sCode, model.getPrecision());

            os << sCode << std::endl;

            // look for spike type events first.
            if (ng.getArchetype().isSpikeEventRequired()) {
                // Create local variable
                os << "bool spikeLikeEvent = false;" << std::endl;

                // Loop through outgoing synapse populations that will contribute to event condition code
                for(const auto &spkEventCond : ng.getArchetype().getSpikeEventCondition()) {
                    // Replace of parameters, derived parameters and extraglobalsynapse parameters
                    Substitutions spkEventCondSubs(&popSubs);

                    addNeuronModelSubstitutions(spkEventCondSubs, ng.getArchetype(), "_pre");

                    std::string eCode = spkEventCond.first;
                    spkEventCondSubs.applyCheckUnreplaced(eCode, "neuronSpkEvntCondition : merged" + std::to_string(ng.getIndex()));
                    eCode = ensureFtype(eCode, model.getPrecision());

                    // Open scope for spike-like event test
                    os << CodeStream::OB(31);

                    // Use synapse population support code namespace if required
                    if (!spkEventCond.second.empty()) {
                        os << " using namespace " << spkEventCond.second << ";" << std::endl;
                    }

                    // Combine this event threshold test with
                    os << "spikeLikeEvent |= (" << eCode << ");" << std::endl;

                    // Close scope for spike-like event test
                    os << CodeStream::CB(31);
                }

                os << "// register a spike-like event" << std::endl;
                os << "if (spikeLikeEvent)";
                {
                    CodeStream::Scope b(os);
                    genEmitSpikeLikeEvent(os, ng, popSubs);
                }
            }

            // test for true spikes if condition is provided
            if (!thCode.empty()) {
                os << "// test for and register a true spike" << std::endl;
                if (nm->isAutoRefractoryRequired()) {
                    os << "if ((" << thCode << ") && !(oldSpike))";
                }
                else {
                    os << "if (" << thCode << ")";
                }
                {
                    CodeStream::Scope b(os);
                    genEmitTrueSpike(os, ng, popSubs);

                    // add after-spike reset if provided
                    if (!nm->getResetCode().empty()) {
                        std::string rCode = nm->getResetCode();
                        neuronSubs.applyCheckUnreplaced(rCode, "resetCode : merged" + std::to_string(ng.getIndex()));
                        rCode = ensureFtype(rCode, model.getPrecision());

                        os << "// spike reset code" << std::endl;
                        os << rCode << std::endl;
                    }
                }

                // Spike triggered variables don't need to be copied
                // if delay isn't required as there's only one copy of them
                if(ng.getArchetype().isDelayRequired()) {
                    // Are there any outgoing synapse groups with axonal delay and presynaptic WUM variables?
                    /*const bool preVars = std::any_of(ng.getOutSyn().cbegin(), ng.getOutSyn().cend(),
                                                    [](const SynapseGroupInternal *sg)
                                                    {
                                                        return (sg->getDelaySteps() != NO_DELAY) && !sg->getWUModel()->getPreVars().empty();
                                                    });

                    // Are there any incoming synapse groups with back-propagation delay and postsynaptic WUM variables?
                    const bool postVars = std::any_of(ng.getInSyn().cbegin(), ng.getInSyn().cend(),
                                                    [](const SynapseGroupInternal *sg)
                                                    {
                                                        return (sg->getBackPropDelaySteps() != NO_DELAY) && !sg->getWUModel()->getPostVars().empty();
                                                    });*/

                    // If spike times, presynaptic variables or postsynaptic variables are required, add if clause
                    if(ng.getArchetype().isSpikeTimeRequired()/* || preVars || postVars*/) {
                        os << "else";
                        CodeStream::Scope b(os);

                        // If spike timing is required, copy spike time from register
                        if(ng.getArchetype().isSpikeTimeRequired()) {
                            assert(false);
                            //os << backend.getVarPrefix() << "sT" << ng.getName() << "[writeDelayOffset + " << popSubs["id"] << "] = lsT;" << std::endl;
                        }

                        // Copy presynaptic WUM variables between delay slots
                        /*for(const auto *sg : ng.getOutSyn()) {
                            if(sg->getDelaySteps() != NO_DELAY) {
                                for(const auto &v : sg->getWUModel()->getPreVars()) {
                                    os << backend.getVarPrefix() << v.name << sg->getName() << "[writeDelayOffset + " << popSubs["id"] <<  "] = ";
                                    os << backend.getVarPrefix() << v.name << sg->getName() << "[readDelayOffset + " << popSubs["id"] << "];" << std::endl;
                                }
                            }
                        }


                        // Copy postsynaptic WUM variables between delay slots
                        for(const auto *sg : ng.getInSyn()) {
                            if(sg->getBackPropDelaySteps() != NO_DELAY) {
                                for(const auto &v : sg->getWUModel()->getPostVars()) {
                                    os << backend.getVarPrefix() << v.name << sg->getName() << "[writeDelayOffset + " << popSubs["id"] <<  "] = ";
                                    os << backend.getVarPrefix() << v.name << sg->getName() << "[readDelayOffset + " << popSubs["id"] << "];" << std::endl;
                                }
                            }
                        }*/
                    }
                }
            }

            // Loop through neuron state variables
            for(const auto &v : nm->getVars()) {
                // If state variables is read/writes - meaning that it may have been updated - or it is delayed -
                // meaning that it needs to be copied into next delay slot whatever - copy neuron state variables
                // back to global state variables dd_V etc  
                const bool delayed = (ng.getArchetype().isVarQueueRequired(v.name) && ng.getArchetype().isDelayRequired());
                if((v.access == VarAccess::READ_WRITE) || delayed) {
                    os << "neuronGroup." << v.name << "[";

                    if (delayed) {
                        os << "writeDelayOffset + ";
                    }
                    os << popSubs["id"] << "] = l" << v.name << ";" << std::endl;
                }
            }

            /*for (const auto &m : ng.getMergedInSyn()) {
                const auto *sg = m.first;
                const auto *psm = sg->getPSModel();

                Substitutions inSynSubs(&neuronSubs);
                inSynSubs.addVarSubstitution("inSyn", "linSyn" + sg->getPSModelTargetName());
                addPostsynapticModelSubstitutions(inSynSubs, sg);

                std::string pdCode = psm->getDecayCode();
                inSynSubs.applyCheckUnreplaced(pdCode, "decayCode : " + sg->getPSModelTargetName());
                pdCode = ensureFtype(pdCode, model.getPrecision());

                os << "// the post-synaptic dynamics" << std::endl;
                if (!psm->getSupportCode().empty()) {
                    os << CodeStream::OB(29) << " using namespace " << sg->getName() << "_postsyn;" << std::endl;
                }
                os << pdCode << std::endl;
                if (!psm->getSupportCode().empty()) {
                    os << CodeStream::CB(29) << " // namespace bracket closed" << std::endl;
                }

                os << backend.getVarPrefix() << "inSyn"  << sg->getPSModelTargetName() << "[" << inSynSubs["id"] << "] = linSyn" << sg->getPSModelTargetName() << ";" << std::endl;

                // Copy any non-readonly postsynaptic model variables back to global state variables dd_V etc
                for (const auto &v : psm->getVars()) {
                    if(v.access == VarAccess::READ_WRITE) {
                        os << backend.getVarPrefix() << v.name << sg->getPSModelTargetName() << "[" << inSynSubs["id"] << "]" << " = lps" << v.name << sg->getPSModelTargetName() << ";" << std::endl;
                    }
                }
            }*/
        },
        // WU var update handler
        [&backend, &model](CodeStream &os, const NeuronGroupMerged &ng, Substitutions &popSubs)
        {
            // Loop through outgoing synaptic populations
            /*for(const auto *sg : ng.getOutSyn()) {
                // If weight update model has any presynaptic update code
                if(!sg->getWUModel()->getPreSpikeCode().empty()) {
                    Substitutions preSubs(&popSubs);

                    CodeStream::Scope b(os);
                    os << "// perform presynaptic update required for " << sg->getName() << std::endl;

                    // Fetch presynaptic variables from global memory
                    for(const auto &v : sg->getWUModel()->getPreVars()) {
                        os << v.type << " l" << v.name << " = ";
                        os << backend.getVarPrefix() << v.name << sg->getName() << "[";
                        if (sg->getDelaySteps() != NO_DELAY) {
                            os << "readDelayOffset + ";
                        }
                        os << preSubs["id"] << "];" << std::endl;
                    }

                    preSubs.addParamValueSubstitution(sg->getWUModel()->getParamNames(), sg->getWUParams());
                    preSubs.addVarValueSubstitution(sg->getWUModel()->getDerivedParams(), sg->getWUDerivedParams());
                    preSubs.addVarNameSubstitution(sg->getWUModel()->getExtraGlobalParams(), "", "", sg->getName());
                    preSubs.addVarNameSubstitution(sg->getWUModel()->getPreVars(), "", "l");

                    const std::string offset = sg->getSrcNeuronGroup()->isDelayRequired() ? "readDelayOffset + " : "";
                    preNeuronSubstitutionsInSynapticCode(preSubs, *sg, offset, "", preSubs["id"], backend.getVarPrefix());

                    // Perform standard substitutions
                    std::string code = sg->getWUModel()->getPreSpikeCode();
                    preSubs.applyCheckUnreplaced(code, "preSpikeCode : " + sg->getName());
                    code = ensureFtype(code, model.getPrecision());
                    os << code;

                    // Loop through presynaptic variables into global memory
                    for(const auto &v : sg->getWUModel()->getPreVars()) {
                        // If state variables is read/write - meaning that it may have been updated - or it is axonally delayed -
                        // meaning that it needs to be copied into next delay slot whatever - copy neuron state variables
                        // back to global state variables dd_V etc  
                        const bool delayed = (sg->getDelaySteps() != NO_DELAY);
                        if((v.access == VarAccess::READ_WRITE) || delayed) {
                            os << backend.getVarPrefix() << v.name << sg->getName() << "[";
                            if (delayed) {
                                os << "writeDelayOffset + ";
                            }
                            os << preSubs["id"] <<  "] = l" << v.name << ";" << std::endl;
                        }
                    }
                }
            }

            // Loop through incoming synaptic populations
            for(const auto *sg : ng.getInSyn()) {
                // If weight update model has any postsynaptic update code
                if(!sg->getWUModel()->getPostSpikeCode().empty()) {
                    Substitutions postSubs(&popSubs);
                    CodeStream::Scope b(os);

                    os << "// perform postsynaptic update required for " << sg->getName() << std::endl;

                    // Fetch postsynaptic variables from global memory
                    for(const auto &v : sg->getWUModel()->getPostVars()) {
                        os << v.type << " l" << v.name << " = ";
                        os << backend.getVarPrefix() << v.name << sg->getName() << "[";
                        if (sg->getBackPropDelaySteps() != NO_DELAY) {
                            os << "readDelayOffset + ";
                        }
                        os << postSubs["id"] << "];" << std::endl;
                    }

                    postSubs.addParamValueSubstitution(sg->getWUModel()->getParamNames(), sg->getWUParams());
                    postSubs.addVarValueSubstitution(sg->getWUModel()->getDerivedParams(), sg->getWUDerivedParams());
                    postSubs.addVarNameSubstitution(sg->getWUModel()->getExtraGlobalParams(), "", "", sg->getName());
                    postSubs.addVarNameSubstitution(sg->getWUModel()->getPostVars(), "", "l");

                    const std::string offset = sg->getTrgNeuronGroup()->isDelayRequired() ? "readDelayOffset + " : "";
                    postNeuronSubstitutionsInSynapticCode(postSubs, *sg, offset, "", postSubs["id"], backend.getVarPrefix());

                    // Perform standard substitutions
                    std::string code = sg->getWUModel()->getPostSpikeCode();
                    postSubs.applyCheckUnreplaced(code, "postSpikeCode : " + sg->getName());
                    code = ensureFtype(code, model.getPrecision());
                    os << code;

                    // Write back presynaptic variables into global memory
                    for(const auto &v : sg->getWUModel()->getPostVars()) {
                        // If state variables is read/write - meaning that it may have been updated - or it is dendritically delayed -
                        // meaning that it needs to be copied into next delay slot whatever - copy neuron state variables
                        // back to global state variables dd_V etc  
                        const bool delayed = (sg->getBackPropDelaySteps() != NO_DELAY);
                        if((v.access == VarAccess::READ_WRITE) || delayed) {
                            os << backend.getVarPrefix() << v.name << sg->getName() << "[";
                            if (delayed) {
                                os << "writeDelayOffset + ";
                            }
                            os << popSubs["id"] <<  "] = l" << v.name << ";" << std::endl;
                        }
                    }
                }
            }*/
        }
    );
}
