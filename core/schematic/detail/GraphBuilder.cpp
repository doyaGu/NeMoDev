#include "core/schematic/detail/GraphBuilder.h"
#include "CKAll.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <stdexcept>

#undef min
#undef max

//==============================================================================
// Construction
//==============================================================================

GraphBuilder::GraphBuilder(InterfaceData &targetData, CKContext *context,
                           bool preserveSavedDirectParameterLinks)
    : m_Data(targetData),
      m_Context(context),
      m_PreserveSavedDirectParameterLinks(preserveSavedDirectParameterLinks) {}

//==============================================================================
// Public Interface
//==============================================================================

void GraphBuilder::BuildGraph(CKBehavior *rootBehavior) {
    if (!rootBehavior)
        return;

    InitializeState();
    m_Data.generatedFromRuntimeGraph = true;
    m_Data.useDevSectionedLayout = false; // Dev 2.5 saves inline version 0x16.
    m_Data.version = 0x16;
    ProcessBehaviorTree(rootBehavior);
    ConfigureParameterLinks();
    m_Data.NotifyObservers(nullptr, InterfaceData::ElementAction::Modified);
}

//==============================================================================
// Initialization
//==============================================================================

void GraphBuilder::InitializeState() {
    m_Data.Clear();
    m_BehaviorMap.clear();
    m_BehaviorDepth.clear();
    m_InputParamSet.clear();
    m_OutputParamSet.clear();
    m_InputParams.clear();
    m_OutputParams.clear();
    m_ShortcutLocalParams.clear();
    m_SharedParameterIndexes.clear();
}

//==============================================================================
// Behavior Tree Traversal
//==============================================================================

void GraphBuilder::ProcessBehaviorTree(CKBehavior *rootBehavior) {
    std::queue<BehaviorQueueEntry> queue;
    queue.push(BehaviorQueueEntry{rootBehavior, 0, -1});

    while (!queue.empty()) {
        const BehaviorQueueEntry current = queue.front();
        queue.pop();

        CKBehavior *behavior = current.behavior;
        const int depth = current.depth;

        // Get or create behavior data
        const bool isRoot = (depth == 0);
        BehaviorData &behaviorData = isRoot ? m_Data.rootBehavior : m_Data.NewBehavior();

        // Register in lookup map
        const CK_ID behaviorId = behavior->GetID();
        m_BehaviorMap[behaviorId] = isRoot ? -1 : static_cast<int>(m_Data.behaviors.size()) - 1;
        m_BehaviorDepth[behaviorId] = depth;

        // Setup behavior and its contents
        SetupBehavior(behaviorData, behavior, depth, current.runtimeSiblingIndex);
        EnqueueSubBehaviors(behavior, depth, queue);
    }
}

void GraphBuilder::EnqueueSubBehaviors(CKBehavior *behavior, int depth,
                                       std::queue<BehaviorQueueEntry> &queue) {
    for (int i = 0, count = behavior->GetSubBehaviorCount(); i < count; ++i) {
        if (auto *subBehavior = behavior->GetSubBehavior(i)) {
            queue.push(BehaviorQueueEntry{subBehavior, depth + 1, i});
        }
    }
}

//==============================================================================
// Element Lookup
//==============================================================================

BehaviorData &GraphBuilder::GetBehaviorData(CK_ID id) {
    const auto found = m_BehaviorMap.find(id);
    if (found == m_BehaviorMap.end()) {
        throw std::runtime_error("Behavior ID is not present in GraphBuilder");
    }
    const int index = found->second;
    if (index >= static_cast<int>(m_Data.behaviors.size())) {
        throw std::runtime_error("Behavior index is outside InterfaceData.behaviors");
    }
    return (index >= 0) ? m_Data.behaviors[index] : m_Data.rootBehavior;
}

//==============================================================================
// Behavior Setup
//==============================================================================

void GraphBuilder::SetupBehavior(BehaviorData &behaviorData, CKBehavior *behavior, int depth, int runtimeSiblingIndex) {
    // Basic properties
    behaviorData.id = behavior->GetID();
    behaviorData.folded = (depth > 0);
    behaviorData.depth = depth;
    behaviorData.parentId = behavior->GetParent() ? behavior->GetParent()->GetID() : 0;
    behaviorData.runtimeSiblingIndex = runtimeSiblingIndex;
    behaviorData.isBehaviorGraph = (behavior->GetType() != CKBEHAVIORTYPE_BASE);
    behaviorData.UpdateRuntimeState(behavior);

    // Process all parameters
    ProcessParameters(behavior);

    // Add graph elements for behavior graphs
    if (behaviorData.isBehaviorGraph) {
        AddBehaviorLinks(behaviorData, behavior);
        AddOperations(behaviorData, behavior);
        AddLocalParameters(behaviorData, behavior);
    }
}

void GraphBuilder::ProcessParameters(CKBehavior *behavior) {
    // Input parameters
    for (int i = 0, count = behavior->GetInputParameterCount(); i < count; ++i) {
        if (auto *param = behavior->GetInputParameter(i)) {
            AddInputParameter(param->GetID());
        }
    }

    // Output parameters
    for (int i = 0, count = behavior->GetOutputParameterCount(); i < count; ++i) {
        if (auto *param = behavior->GetOutputParameter(i)) {
            AddOutputParameter(param->GetID());
        }
    }

    // Target parameter
    if (behavior->IsUsingTarget()) {
        if (auto *targetParam = behavior->GetTargetParameter()) {
            AddInputParameter(targetParam->GetID());
        }
    }

    // Operation parameters
    for (int i = 0, count = behavior->GetParameterOperationCount(); i < count; ++i) {
        ProcessOperationParameters(behavior->GetParameterOperation(i));
    }
}

void GraphBuilder::ProcessOperationParameters(CKParameterOperation *operation) {
    if (!operation) return;

    if (auto *in1 = operation->GetInParameter1()) AddInputParameter(in1->GetID());
    if (auto *in2 = operation->GetInParameter2()) AddInputParameter(in2->GetID());
    if (auto *out = operation->GetOutParameter())  AddOutputParameter(out->GetID());
}

//==============================================================================
// Parameter Tracking
//==============================================================================

void GraphBuilder::AddInputParameter(CK_ID paramId) {
    if (m_InputParamSet.insert(paramId).second) {
        m_InputParams.push_back(paramId);
    }
}

void GraphBuilder::AddOutputParameter(CK_ID paramId) {
    if (m_OutputParamSet.insert(paramId).second) {
        m_OutputParams.push_back(paramId);
    }
}

//==============================================================================
// Behavior Graph Elements
//==============================================================================

void GraphBuilder::AddBehaviorLinks(BehaviorData &behaviorData, CKBehavior *behavior) {
    for (int i = 0, count = behavior->GetSubBehaviorLinkCount(); i < count; ++i) {
        auto *behaviorLink = behavior->GetSubBehaviorLink(i);
        if (!behaviorLink) continue;
        auto *inputIO = behaviorLink->GetInBehaviorIO();
        auto *outputIO = behaviorLink->GetOutBehaviorIO();
        if (!inputIO || !inputIO->GetOwner() || !outputIO || !outputIO->GetOwner()) {
            // The missing link is reported by ValidateInterfaceData.
            m_Context->OutputToConsoleEx(
                (CKSTRING)"Error: behavior link %d has no owned BehaviorIO endpoint", behaviorLink->GetID());
            continue;
        }
        behaviorData.AddLink(CreateBehaviorLink(behaviorLink));
    }
}

Link GraphBuilder::CreateBehaviorLink(CKBehaviorLink *behaviorLink) {
    // Extract start endpoint (from behavior output or input)
    auto *inputIO = behaviorLink->GetInBehaviorIO();
    auto *inputBehavior = inputIO->GetOwner();

    LinkEndpoint start{inputBehavior->GetID(), inputBehavior->GetOutputPosition(inputIO), ENDPOINT_BOUT};
    if (start.index == -1) {
        start.index = inputBehavior->GetInputPosition(inputIO);
        start.type = (inputBehavior->GetType() == CKBEHAVIORTYPE_SCRIPT) ? ENDPOINT_START_BIN : ENDPOINT_BIN;
        if (start.type == ENDPOINT_START_BIN) {
            start.id = inputIO->GetID();
        }
    }

    // Extract end endpoint
    auto *outputIO = behaviorLink->GetOutBehaviorIO();
    auto *outputBehavior = outputIO->GetOwner();

    LinkEndpoint end{outputBehavior->GetID(), outputBehavior->GetInputPosition(outputIO), ENDPOINT_BIN};
    if (end.index == -1) {
        end.index = outputBehavior->GetOutputPosition(outputIO);
        end.type = ENDPOINT_BOUT;
    }

    return {behaviorLink->GetID(), LINK_TYPE_BEHAVIOR, start, end};
}

void GraphBuilder::AddOperations(BehaviorData &behaviorData, CKBehavior *behavior) {
    for (int i = 0, count = behavior->GetParameterOperationCount(); i < count; ++i) {
        if (auto *operation = behavior->GetParameterOperation(i)) {
            behaviorData.AddOperation(Operation(operation->GetID()));
        }
    }
}

void GraphBuilder::AddLocalParameters(BehaviorData &behaviorData, CKBehavior *behavior) {
    for (int i = 0, count = behavior->GetLocalParameterCount(); i < count; ++i) {
        if (auto *localParam = behavior->GetLocalParameter(i)) {
            behaviorData.AddLocalParameter(Parameter(localParam->GetID(), PARAM_STYLE_NAME));
        }
    }
}

//==============================================================================
// Parameter Position Resolution
//==============================================================================

GraphBuilder::ParameterPosition GraphBuilder::GetInputParameterPosition(
    CKParameterIn *inputParam, CKBehavior **owner) {

    auto *ownerObject = inputParam->GetOwner();
    const CK_CLASSID classId = ownerObject ? ownerObject->GetClassID() : 0;

    if (classId == CKCID_BEHAVIOR) {
        auto *ownerBeh = static_cast<CKBehavior *>(ownerObject);
        int index = ownerBeh->GetInputParameterPosition(inputParam);

        // Target parameter has special index -2
        if (ownerBeh->IsUsingTarget() &&
            ownerBeh->GetTargetParameter() == inputParam) {
            index = -2;
        }

        // Script parameters are represented in the Script's own
        // InterfaceChunk; a root Script has no parent CKBehavior.
        *owner = ownerBeh->GetParent() ? ownerBeh->GetParent() : ownerBeh;
        return {ownerObject->GetID(), index, (*owner)->GetID()};
    }

    if (classId == CKCID_PARAMETEROPERATION) {
        auto *operation = static_cast<CKParameterOperation *>(ownerObject);
        *owner = operation->GetOwner();
        if (!*owner) {
            throw std::runtime_error("Parameter operation has no owner behavior");
        }
        const int index = (operation->GetInParameter1() == inputParam) ? 0 : 1;
        return {ownerObject->GetID(), index, (*owner)->GetID()};
    }

    m_Context->OutputToConsoleEx((CKSTRING)"Error: Unknown owner type for input parameter");
    throw std::runtime_error("Unknown owner type for input parameter");
}

GraphBuilder::ParameterPosition GraphBuilder::GetOutputParameterPosition(
    CKParameterOut *outputParam, CKBehavior **ownerBehavior) {

    auto *ownerObject = outputParam->GetOwner();
    const CK_CLASSID classId = ownerObject ? ownerObject->GetClassID() : 0;

    if (classId == CKCID_BEHAVIOR) {
        auto *ownerBeh = static_cast<CKBehavior *>(ownerObject);
        *ownerBehavior = ownerBeh->GetParent() ? ownerBeh->GetParent() : ownerBeh;
        return {ownerObject->GetID(), ownerBeh->GetOutputParameterPosition(outputParam), (*ownerBehavior)->GetID()};
    }

    if (classId == CKCID_PARAMETEROPERATION) {
        auto *operation = static_cast<CKParameterOperation *>(ownerObject);
        *ownerBehavior = operation->GetOwner();
        if (!*ownerBehavior) {
            throw std::runtime_error("Parameter operation has no owner behavior");
        }
        return {ownerObject->GetID(), 0, (*ownerBehavior)->GetID()};
    }

    m_Context->OutputToConsoleEx((CKSTRING)"Error: Unknown owner type for output parameter");
    throw std::runtime_error("Unknown owner type for output parameter");
}

GraphBuilder::ParameterPosition GraphBuilder::GetLocalParameterPosition(CKParameterLocal *localParam) {
    auto *ownerObject = localParam->GetOwner();

    if (!ownerObject || ownerObject->GetClassID() != CKCID_BEHAVIOR) {
        m_Context->OutputToConsoleEx((CKSTRING)"Error: Local parameter owner is not a behavior");
        throw std::runtime_error("Local parameter owner is not a behavior");
    }

    auto *ownerBeh = static_cast<CKBehavior *>(ownerObject);
    const CK_ID behId = ownerBeh->GetID();
    return {behId, ownerBeh->GetLocalParameterPosition(localParam), behId};
}

LinkEndpoint GraphBuilder::MakeInputParameterEndpoint(
    const ParameterPosition &position) {
    return {
        position.id,
        position.index,
        position.index == -2 ? ENDPOINT_TARGET_PIN : ENDPOINT_PIN
    };
}

LinkEndpoint GraphBuilder::MakeOutputParameterEndpoint(
    const ParameterPosition &position) {
    return {position.id, position.index, ENDPOINT_POUT};
}

GraphBuilder::ParameterPosition GraphBuilder::CreateShortcutParameterPosition(
    CK_ID behaviorId, CK_ID sourceId, ParameterStyle style) {

    BehaviorData &behaviorData = GetBehaviorData(behaviorId);
    auto &sourceIndexes = m_SharedParameterIndexes[behaviorId];
    const auto existing = sourceIndexes.find(sourceId);
    if (existing != sourceIndexes.end()) {
        return {behaviorId, existing->second, behaviorId};
    }

    Parameter paramData(sourceId, style);
    paramData.sourceId = sourceId;
    behaviorData.AddSharedParameter(paramData);

    const int index = static_cast<int>(behaviorData.sharedParams.size()) - 1;
    sourceIndexes[sourceId] = index;
    return {behaviorId, index, behaviorId};
}

void GraphBuilder::CollectShortcutLocalParameters() {
    std::unordered_set<CK_ID> writtenLocals;
    std::unordered_set<CK_ID> readLocals;

    const XObjectPointerArray &outputObjects = m_Context->GetObjectListByType(CKCID_PARAMETEROUT, TRUE);
    const int outputCount = outputObjects.Size();
    for (int i = 0; i < outputCount; ++i) {
        auto *outputParam = CKParameterOut::Cast(outputObjects.GetObject(i));
        if (!outputParam) continue;

        for (int j = 0, count = outputParam->GetDestinationCount(); j < count; ++j) {
            auto *destination = outputParam->GetDestination(j);
            if (CKParameterLocal::Cast(destination)) {
                writtenLocals.insert(destination->GetID());
            }
        }
    }

    for (const CK_ID id : m_InputParams) {
        auto *object = m_Context->GetObject(id);
        auto *inputParam = CKParameterIn::Cast(object);
        if (!inputParam) continue;

        auto *sourceParam = inputParam->GetDirectSource();
        if (CKParameterLocal::Cast(sourceParam)) {
            readLocals.insert(sourceParam->GetID());
        }
    }

    m_ShortcutLocalParams.clear();
    for (const CK_ID id : writtenLocals) {
        if (readLocals.find(id) != readLocals.end()) {
            m_ShortcutLocalParams.insert(id);
        }
    }
}

bool GraphBuilder::ShouldUseShortcutForLocal(CK_ID localParamId) const {
    return m_ShortcutLocalParams.find(localParamId) != m_ShortcutLocalParams.end();
}

bool GraphBuilder::ShouldCreateDirectParameterLink(
    CKParameterIn *inputParam,
    CKParameter *sourceParam) const {

    if (!inputParam || !sourceParam) {
        return false;
    }

    if (CKParameterLocal::Cast(sourceParam)) {
        return true;
    }

    const CKParameterType sourceType = sourceParam->GetType();
    const CKParameterType targetType = inputParam->GetType();
    if (sourceType < 0) {
        return m_PreserveSavedDirectParameterLinks;
    }
    if (targetType < 0) {
        return m_PreserveSavedDirectParameterLinks;
    }

    if (m_PreserveSavedDirectParameterLinks) {
        return true;
    }

    CKParameterManager *parameterManager = m_Context ? m_Context->GetParameterManager() : nullptr;
    if (!parameterManager) {
        return true;
    }

    return parameterManager->IsTypeCompatible(sourceType, targetType) != FALSE;
}

//==============================================================================
// Parameter Link Configuration
//==============================================================================

void GraphBuilder::ConfigureParameterLinks() {
    ParameterChain inputChain;
    ParameterChain outputChain;

    BuildInputParameterChains(inputChain);
    BuildOutputParameterChains(outputChain);
    CollectShortcutLocalParameters();
    ConnectParameterChains(inputChain, outputChain);
    ConnectOutputDestinations(outputChain);
}

//==============================================================================
// Parameter Chain Building
//==============================================================================

void GraphBuilder::BuildInputParameterChains(ParameterChain &inputChain) {
    for (const CK_ID id : m_InputParams) {
        auto *inputParam = static_cast<CKParameterIn *>(m_Context->GetObject(id));
        if (!inputParam) continue;

        try {
            BuildInputParameterChain(inputParam, inputChain);
        } catch (const std::exception &e) {
            m_Context->OutputToConsoleEx(
                (CKSTRING)"Error building input parameter chain for %d: %s", id, e.what());
        }
    }
}

void GraphBuilder::BuildInputParameterChain(CKParameterIn *inputParam, ParameterChain &inputChain) {
    CKBehavior *beh = nullptr;
    auto &positionChain = inputChain[inputParam->GetID()];

    // Get initial position
    positionChain.push_back(GetInputParameterPosition(inputParam, &beh));

    const auto &firstPos = positionChain.back();
    LinkEndpoint lastEndpoint = MakeInputParameterEndpoint(firstPos);

    // Traverse up through parent behaviors
    while (beh && beh->GetParent() && beh->GetInputParameterPosition(inputParam) != -1) {
        const CK_ID behId = beh->GetID();
        const int index = beh->GetInputParameterPosition(inputParam);
        const CK_ID parentId = beh->GetParent()->GetID();

        positionChain.push_back({behId, index, parentId});

        // Create hierarchy link with highlight flag (parameter operation link)
        const LinkEndpoint start{behId, index, ENDPOINT_PIN};
        Link link{0, LINK_TYPE_PARAMETER, start, lastEndpoint, true};
        GetBehaviorData(behId).AddLink(link);

        lastEndpoint = start;
        beh = beh->GetParent();
    }
}

void GraphBuilder::BuildOutputParameterChains(ParameterChain &outputChain) {
    for (const CK_ID id : m_OutputParams) {
        auto *outputParam = static_cast<CKParameterOut *>(m_Context->GetObject(id));
        if (!outputParam) continue;

        try {
            BuildOutputParameterChain(outputParam, outputChain);
        } catch (const std::exception &e) {
            m_Context->OutputToConsoleEx(
                (CKSTRING)"Error building output parameter chain for %d: %s", id, e.what());
        }
    }
}

void GraphBuilder::BuildOutputParameterChain(CKParameterOut *outputParam, ParameterChain &outputChain) {
    CKBehavior *beh = nullptr;
    auto &positionChain = outputChain[outputParam->GetID()];

    // Get initial position
    positionChain.push_back(GetOutputParameterPosition(outputParam, &beh));

    const auto &firstPos = positionChain.back();
    LinkEndpoint lastEndpoint = MakeOutputParameterEndpoint(firstPos);

    // Traverse up through parent behaviors
    while (beh && beh->GetParent() && beh->GetOutputParameterPosition(outputParam) != -1) {
        const CK_ID behId = beh->GetID();
        const int index = beh->GetOutputParameterPosition(outputParam);
        const CK_ID parentId = beh->GetParent()->GetID();

        positionChain.push_back({behId, index, parentId});

        // Create hierarchy link with highlight flag (parameter operation link)
        const LinkEndpoint end{behId, index, ENDPOINT_POUT};
        Link link{0, LINK_TYPE_PARAMETER, lastEndpoint, end, true};
        GetBehaviorData(behId).AddLink(link);

        lastEndpoint = end;
        beh = beh->GetParent();
    }
}

//==============================================================================
// Parameter Chain Connection
//==============================================================================

void GraphBuilder::ConnectParameterChains(
    const ParameterChain &inputChain, const ParameterChain &outputChain) {

    for (const CK_ID id : m_InputParams) {
        auto *inputParam = static_cast<CKParameterIn *>(m_Context->GetObject(id));
        if (!inputParam) continue;

        try {
            CKBehavior *beh = nullptr;
            auto position = GetInputParameterPosition(inputParam, &beh);

            // Find this parameter's position chain
            auto inputIt = inputChain.find(inputParam->GetID());
            if (inputIt == inputChain.end()) continue;
            const auto &inputPositions = inputIt->second;

            // Handle direct source (output parameter) or shared source (another input)
            if (auto *sourceParam = inputParam->GetDirectSource()) {
                ConnectToDirectSource(inputParam, sourceParam, inputPositions, position, outputChain);
            } else if (auto *sharedInput = inputParam->GetSharedSource()) {
                ConnectToSharedSource(inputParam, sharedInput, inputPositions, inputChain);
            }
        } catch (const std::exception &e) {
            m_Context->OutputToConsoleEx((CKSTRING)"Error connecting parameter %d: %s", id, e.what());
        }
    }
}

void GraphBuilder::ConnectOutputDestinations(const ParameterChain &outputChain) {
    auto addIfMissing = [](BehaviorData &behaviorData, const Link &link) {
        const auto exists = std::find_if(
            behaviorData.links.begin(), behaviorData.links.end(),
            [&link](const Link &existing) {
                return existing.type == link.type &&
                       existing.highlight == link.highlight &&
                       existing.start.id == link.start.id &&
                       existing.start.index == link.start.index &&
                       existing.start.type == link.start.type &&
                       existing.end.id == link.end.id &&
                       existing.end.index == link.end.index &&
                       existing.end.type == link.end.type;
            });
        if (exists == behaviorData.links.end()) {
            behaviorData.AddLink(link);
        }
    };

    for (const CK_ID id : m_OutputParams) {
        auto *outputParam = CKParameterOut::Cast(m_Context->GetObject(id));
        if (!outputParam) continue;

        // An empty chain was already reported by BuildOutputParameterChains.
        const auto chain = outputChain.find(id);
        if (chain == outputChain.end() || chain->second.empty()) continue;
        const std::vector<ParameterPosition> &sourcePositions = chain->second;

        // Each chain position records a graph that displays the source,
        // deepest first. Find the first one that displays the destination.
        auto findSourceDisplayedIn = [&sourcePositions](CK_ID graphId, CK_ID otherGraphId) {
            const auto found = std::find_if(sourcePositions.begin(), sourcePositions.end(),
                [graphId, otherGraphId](const ParameterPosition &position) {
                    return position.behaviorId == graphId ||
                           (otherGraphId != 0 && position.behaviorId == otherGraphId);
                });
            return found != sourcePositions.end() ? &*found : nullptr;
        };

        for (int i = 0, count = outputParam->GetDestinationCount(); i < count; ++i) {
            auto *destination = outputParam->GetDestination(i);
            if (!destination) continue;

            try {
                const ParameterPosition *source = nullptr;
                LinkEndpoint end;

                if (auto *localDestination = CKParameterLocal::Cast(destination)) {
                    const auto destPos = GetLocalParameterPosition(localDestination);
                    source = findSourceDisplayedIn(destPos.behaviorId, 0);
                    if (source) end = {destPos.id, destPos.index, ENDPOINT_PLOCAL};
                } else if (auto *outputDestination = CKParameterOut::Cast(destination)) {
                    // Object attribute parameters are owned by a CKBeObject
                    // and have no display of their own.
                    CKObject *owner = outputDestination->GetOwner();
                    const CK_CLASSID ownerClass = owner ? owner->GetClassID() : 0;
                    if (ownerClass == CKCID_BEHAVIOR || ownerClass == CKCID_PARAMETEROPERATION) {
                        CKBehavior *destGraph = nullptr;
                        const auto destPos = GetOutputParameterPosition(outputDestination, &destGraph);
                        // A behavior graph also displays its own outputs.
                        source = findSourceDisplayedIn(
                            destPos.behaviorId, ownerClass == CKCID_BEHAVIOR ? destPos.id : 0);
                        if (source) end = {destPos.id, destPos.index, ENDPOINT_POUT};
                    }
                } else {
                    continue;
                }

                // When no graph displays both ends (a local or output of
                // another graph or Script, or an attribute parameter), Dev 2.5
                // saves the connection in the producer's graph, ending at a
                // shortcut of the destination (Set Global Animation Step,
                // Time Settings and TableFanScript samples).
                const ParameterPosition &start = source ? *source : sourcePositions.front();
                if (m_BehaviorMap.find(start.behaviorId) == m_BehaviorMap.end()) continue;
                if (!source) {
                    const auto shortcut = CreateShortcutParameterPosition(
                        start.behaviorId, destination->GetID(), PARAM_STYLE_NAMEVALUE);
                    end = {shortcut.id, shortcut.index, ENDPOINT_POUT_SHORTCUT};
                }
                addIfMissing(GetBehaviorData(start.behaviorId),
                             Link{0, LINK_TYPE_PARAMETER, MakeOutputParameterEndpoint(start), end});
            } catch (const std::exception &e) {
                m_Context->OutputToConsoleEx(
                    (CKSTRING)"Error connecting output parameter %d to %d: %s",
                    id, destination->GetID(), e.what());
            }
        }
    }
}

void GraphBuilder::ConnectToDirectSource(
    CKParameterIn *inputParam, CKParameter *sourceParam,
    const std::vector<ParameterPosition> &inputPositions,
    const ParameterPosition &position,
    const ParameterChain &outputChain) {

    if (!ShouldCreateDirectParameterLink(inputParam, sourceParam)) {
        return;
    }

    // Build source positions
    std::vector<ParameterPosition> sourcePositions;

    auto sourceIt = outputChain.find(sourceParam->GetID());
    if (sourceIt != outputChain.end()) {
        sourcePositions = sourceIt->second;
    } else if (auto *localSource = CKParameterLocal::Cast(sourceParam)) {
        sourcePositions.push_back(GetLocalParameterPosition(localSource));
    }

    // Try direct connection first, then shortcut
    if (!ConnectParametersInSameBehavior(inputPositions, sourcePositions, sourceParam)) {
        CreateParameterShortcut(position, sourceParam);
    }
}

void GraphBuilder::ConnectToSharedSource(
    CKParameterIn *inputParam, CKParameterIn *sharedInput,
    const std::vector<ParameterPosition> &inputPositions,
    const ParameterChain &inputChain) {

    const auto sharedIt = inputChain.find(sharedInput->GetID());
    const ParameterPosition *inputPosition = nullptr;
    const ParameterPosition *sharedPosition = nullptr;
    if (sharedIt != inputChain.end() &&
        FindDeepestSharedInputPositions(
            inputPositions,
            sharedIt->second,
            inputPosition,
            sharedPosition)) {
        const LinkEndpoint start = MakeInputParameterEndpoint(*sharedPosition);
        const LinkEndpoint end = MakeInputParameterEndpoint(*inputPosition);
        GetBehaviorData(inputPosition->behaviorId).AddLink(
            {0, LINK_TYPE_PARAMETER, start, end});
        return;
    }

    // Dev 2.5 rebuilds a shared source only as an input of the enclosing
    // graph (0x53A710); AppendRuntimeGraphLinkRecord (0x539DF0) counts any
    // other shared input as an invalid endpoint and creates no link. A
    // shortcut cannot represent it: its source must be a CKParameter.
    m_Context->OutputToConsoleEx(
        (CKSTRING)"Cannot connect shared parameters %d <-> %d, source type is %d",
        inputParam->GetID(), sharedInput->GetID(), sharedInput->GetClassID());
}

bool GraphBuilder::ConnectParametersInSameBehavior(
    const std::vector<ParameterPosition> &inputPositions,
    const std::vector<ParameterPosition> &sourcePositions,
    CKParameter *sourceParam) {

    const ParameterPosition *inputPosition = nullptr;
    const ParameterPosition *sourcePosition = nullptr;
    if (!FindDeepestMatchingPositions(
            inputPositions,
            sourcePositions,
            inputPosition,
            sourcePosition)) {
        return false;
    }

    LinkEndpoint start;
    if (CKParameterLocal::Cast(sourceParam) &&
        ShouldUseShortcutForLocal(sourceParam->GetID())) {
        const auto shortcut = CreateShortcutParameterPosition(
            inputPosition->behaviorId, sourceParam->GetID(), PARAM_STYLE_NAME);
        start = {shortcut.id, shortcut.index, ENDPOINT_POUT_SHORTCUT};
    } else if (CKParameterLocal::Cast(sourceParam)) {
        start = {sourcePosition->id, sourcePosition->index, ENDPOINT_PLOCAL};
    } else {
        start = MakeOutputParameterEndpoint(*sourcePosition);
    }

    const LinkEndpoint end = MakeInputParameterEndpoint(*inputPosition);
    GetBehaviorData(inputPosition->behaviorId).AddLink(
        {0, LINK_TYPE_PARAMETER, start, end});
    return true;
}

bool GraphBuilder::FindDeepestMatchingPositions(
    const std::vector<ParameterPosition> &inputPositions,
    const std::vector<ParameterPosition> &sourcePositions,
    const ParameterPosition *&inputPosition,
    const ParameterPosition *&sourcePosition) const {
    std::unordered_map<CK_ID, const ParameterPosition *> sourceByBehavior;
    for (const auto &position : sourcePositions) {
        sourceByBehavior.emplace(position.behaviorId, &position);
    }

    int bestDepth = std::numeric_limits<int>::min();
    inputPosition = nullptr;
    sourcePosition = nullptr;
    for (const auto &position : inputPositions) {
        const auto source = sourceByBehavior.find(position.behaviorId);
        if (source == sourceByBehavior.end()) {
            continue;
        }
        const auto depth = m_BehaviorDepth.find(position.behaviorId);
        const int currentDepth = depth != m_BehaviorDepth.end()
            ? depth->second
            : -1;
        if (!inputPosition || currentDepth > bestDepth) {
            bestDepth = currentDepth;
            inputPosition = &position;
            sourcePosition = source->second;
        }
    }
    return inputPosition && sourcePosition;
}

bool GraphBuilder::FindDeepestSharedInputPositions(
    const std::vector<ParameterPosition> &inputPositions,
    const std::vector<ParameterPosition> &sharedPositions,
    const ParameterPosition *&inputPosition,
    const ParameterPosition *&sharedPosition) const {
    std::unordered_map<CK_ID, const ParameterPosition *> sharedByOwner;
    for (const auto &position : sharedPositions) {
        sharedByOwner.emplace(position.id, &position);
    }

    int bestDepth = std::numeric_limits<int>::min();
    inputPosition = nullptr;
    sharedPosition = nullptr;
    for (const auto &position : inputPositions) {
        const auto shared = sharedByOwner.find(position.behaviorId);
        if (shared == sharedByOwner.end()) {
            continue;
        }
        const auto depth = m_BehaviorDepth.find(position.behaviorId);
        const int currentDepth = depth != m_BehaviorDepth.end()
            ? depth->second
            : -1;
        if (!inputPosition || currentDepth > bestDepth) {
            bestDepth = currentDepth;
            inputPosition = &position;
            sharedPosition = shared->second;
        }
    }
    return inputPosition && sharedPosition;
}

void GraphBuilder::CreateParameterShortcut(
    const ParameterPosition &position, CKParameter *sourceParam) {

    auto shortcutPos = CreateShortcutParameterPosition(
        position.behaviorId, sourceParam->GetID(), PARAM_STYLE_NAME);

    LinkEndpoint start{position.behaviorId, shortcutPos.index, ENDPOINT_POUT_SHORTCUT};
    LinkEndpoint end = MakeInputParameterEndpoint(position);

    GetBehaviorData(position.behaviorId).AddLink({0, LINK_TYPE_PARAMETER, start, end});
}
