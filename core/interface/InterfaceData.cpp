#include "core/interface/InterfaceData.h"
#include "core/interface/InterfaceChunkFormat.h"
#include "core/interface/detail/InterfaceBitmap.h"
#include "core/interface/detail/InterfaceChunkSections.h"

#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <algorithm>
#include <sstream>

namespace Format = InterfaceChunkFormat;

Point Header::GetPosition() const {
    return {hStartPos, vStartPos};
}

void Header::SetPosition(float h, float v) {
    hStartPos = h;
    vStartPos = v;
}

void Header::SetSnapshot(void *bitmap, const std::shared_ptr<void> &owner) {
    if (owner && owner.get() == bitmap) snapshotStorage = InterfaceBitmap::Retain(owner);
    else if (snapshotStorage) snapshotStorage = snapshotStorage->WithoutEncoding(bitmap);
    snapshot = bitmap;
}

std::shared_ptr<void> Header::SnapshotOwner() const {
    return snapshotStorage && snapshot == snapshotStorage->GetHandle() ? snapshotStorage->GetOwner() : nullptr;
}

// LinkEndpoint implementations
bool LinkEndpoint::IsParameterInput() const {
    return !hasLegacyTypeCode && (type == ENDPOINT_PIN || type == ENDPOINT_TARGET_PIN);
}

bool LinkEndpoint::IsParameterOutput() const {
    return !hasLegacyTypeCode && (type == ENDPOINT_POUT || type == ENDPOINT_POUT_SHORTCUT);
}

bool LinkEndpoint::IsParameterOutputShortcut() const {
    return !hasLegacyTypeCode && type == ENDPOINT_POUT_SHORTCUT;
}

bool LinkEndpoint::IsParameterLocal() const {
    return !hasLegacyTypeCode && type == ENDPOINT_PLOCAL;
}

bool LinkEndpoint::IsBehaviorInput() const {
    return !hasLegacyTypeCode && (type == ENDPOINT_BIN || type == ENDPOINT_START_BIN);
}

bool LinkEndpoint::IsStartBehaviorInput() const {
    return !hasLegacyTypeCode && type == ENDPOINT_START_BIN;
}

bool LinkEndpoint::IsBehaviorOutput() const {
    return !hasLegacyTypeCode && type == ENDPOINT_BOUT;
}

bool LinkEndpoint::IsParameterRelated() const {
    return IsParameterInput() || IsParameterOutput() || IsParameterLocal();
}

bool LinkEndpoint::IsBehaviorRelated() const {
    return IsBehaviorInput() || IsBehaviorOutput();
}

bool LinkEndpoint::IsCompatibleWith(const LinkEndpoint &other) const {
    return (IsParameterOutput() && other.IsParameterInput()) ||
           (IsBehaviorOutput() && other.IsBehaviorInput());
}

// Link implementations
void Link::AddControlPoint(const Point &point) {
    points.push_back(point);
}

void Link::InsertControlPoint(int index, const Point &point) {
    if (index >= 0 && index <= static_cast<int>(points.size())) {
        points.insert(points.begin() + index, point);
    }
}

void Link::RemoveControlPoint(int index) {
    if (index >= 0 && index < static_cast<int>(points.size())) {
        points.erase(points.begin() + index);
    }
}

void Link::UpdateControlPoint(int index, const Point &point) {
    if (index >= 0 && index < static_cast<int>(points.size())) {
        points[index] = point;
    }
}

bool Link::IsBehaviorLink() const {
    return type == LINK_TYPE_BEHAVIOR;
}

bool Link::IsParameterLink() const {
    return type == LINK_TYPE_PARAMETER;
}

bool Link::IsHighlighted() const {
    return highlight;
}

void Link::SetHighlighted(bool highlighted) {
    highlight = highlighted;
}

bool Link::IsParameterOpLink() const {
    return IsParameterLink() && IsHighlighted();
}

void Link::Offset(float h, float v) {
    for (Point &point : points) {
        point.h += h;
        point.v += v;
    }
}

void Link::Scale(float factor, const Point &center) {
    for (Point &point : points) {
        point.h = center.h + (point.h - center.h) * factor;
        point.v = center.v + (point.v - center.v) * factor;
    }
}

float Link::GetPathLength() const {
    float length = 0.0f;
    for (size_t i = 1; i < points.size(); ++i) {
        length += points[i].DistanceTo(points[i - 1]);
    }
    return length;
}

Point Link::GetPointAlong(float t) const {
    if (points.empty()) return {};
    if (points.size() == 1 || t <= 0.0f) return points.front();
    if (t >= 1.0f) return points.back();

    const float targetDistance = GetPathLength() * t;
    float currentDistance = 0.0f;
    for (size_t i = 1; i < points.size(); ++i) {
        const float segmentLength = points[i].DistanceTo(points[i - 1]);
        if (currentDistance + segmentLength >= targetDistance) {
            const float segmentT = (targetDistance - currentDistance) / segmentLength;
            return points[i - 1].Lerp(points[i], segmentT);
        }
        currentDistance += segmentLength;
    }
    return points.back();
}

bool Link::PassesNear(const Point &point, float maxDistance) const {
    for (size_t i = 1; i < points.size(); ++i) {
        const Point &start = points[i - 1];
        const Point &end = points[i];
        const float length = start.DistanceTo(end);
        if (length < 0.0001f) {
            if (point.DistanceTo(start) <= maxDistance) return true;
            continue;
        }

        const float t = ((point.h - start.h) * (end.h - start.h) +
                         (point.v - start.v) * (end.v - start.v)) /
                        (length * length);
        if (t <= 0.0f) {
            if (point.DistanceTo(start) <= maxDistance) return true;
        } else if (t >= 1.0f) {
            if (point.DistanceTo(end) <= maxDistance) return true;
        } else {
            const Point projection(
                start.h + t * (end.h - start.h),
                start.v + t * (end.v - start.v));
            if (point.DistanceTo(projection) <= maxDistance) return true;
        }
    }
    return false;
}

void Operation::SetPosition(float h, float v) {
    hPos = h;
    vPos = v;
}

Point Operation::GetPosition() const {
    return {hPos, vPos};
}

// Comment implementations
Rect Comment::GetRect() const {
    return {hPos, vPos, width, height};
}

void Comment::SetRect(const Rect &rect) {
    hPos = rect.hPos;
    vPos = rect.vPos;
    width = rect.hSize;
    height = rect.vSize;
}

bool Comment::IsCollapsed() const {
    return (styleFlags & COMMENT_STYLE_COLLAPSED) != 0;
}

bool Comment::IsLocked() const {
    return (styleFlags & COMMENT_STYLE_LOCKED) != 0;
}

bool Comment::IsTransparent() const {
    return (styleFlags & COMMENT_STYLE_TRANSPARENT) != 0;
}

void Comment::SetCollapsed(bool collapsed) {
    styleFlags = collapsed ? styleFlags | COMMENT_STYLE_COLLAPSED
                           : styleFlags & ~COMMENT_STYLE_COLLAPSED;
}

void Comment::SetLocked(bool locked) {
    styleFlags = locked ? styleFlags | COMMENT_STYLE_LOCKED
                        : styleFlags & ~COMMENT_STYLE_LOCKED;
}

void Comment::SetTransparent(bool transparent) {
    styleFlags = transparent ? styleFlags | COMMENT_STYLE_TRANSPARENT
                             : styleFlags & ~COMMENT_STYLE_TRANSPARENT;
}

void Comment::ResizeToFitText(
    float charWidth,
    float lineHeight,
    float hPadding,
    float vPadding) {
    std::vector<std::string> lines;
    std::string currentLine;
    for (char character : text) {
        if (character == '\n') {
            lines.push_back(currentLine);
            currentLine.clear();
        } else {
            currentLine += character;
        }
    }
    if (!currentLine.empty()) lines.push_back(currentLine);

    float maxLineWidth = 0.0f;
    for (const std::string &line : lines) {
        maxLineWidth = std::max(
            maxLineWidth,
            static_cast<float>(line.length()) * charWidth);
    }
    width = maxLineWidth + hPadding * 2.0f;
    height = static_cast<float>(lines.size()) * lineHeight + vPadding * 2.0f;
}

// Parameter implementations
bool Parameter::IsShortcut() const {
    return sourceId != 0 && sourceId != static_cast<CK_ID>(-1);
}

void Parameter::SetPosition(int h, int v) {
    hPos = h;
    vPos = v;
}

Point Parameter::GetPosition() const {
    return {static_cast<float>(hPos), static_cast<float>(vPos)};
}

bool Parameter::HasStyleFlag(ParameterStyle flag) const {
    return (style & flag) != 0;
}

void Parameter::SetStyleFlag(ParameterStyle flag, bool value) {
    style = value ? static_cast<ParameterStyle>(style | flag)
                  : static_cast<ParameterStyle>(style & ~flag);
}

bool Parameter::ShowsName() const {
    return HasStyleFlag(PARAM_STYLE_NAME) || HasStyleFlag(PARAM_STYLE_NAMEVALUE);
}

bool Parameter::ShowsValue() const {
    return HasStyleFlag(PARAM_STYLE_VALUE) || HasStyleFlag(PARAM_STYLE_NAMEVALUE);
}

bool Parameter::IsClosed() const {
    return HasStyleFlag(PARAM_STYLE_COLLAPSED);
}

void Parameter::SetClosed(bool closed) {
    SetStyleFlag(PARAM_STYLE_COLLAPSED, closed);
}

ExtraDataComparisonType ExtraSubData::GetComparisonType() const {
    return static_cast<ExtraDataComparisonType>(value1);
}

ExtraDataLogicalOperator ExtraSubData::GetLogicalOperator() const {
    return static_cast<ExtraDataLogicalOperator>(value2);
}

bool ExtraSubData::UsesParameterReference() const {
    return InterfaceChunkFormat::ExtraSubDataValueStoresObjectId(value1);
}

void ExtraSubData::SetComparisonType(ExtraDataComparisonType comparison) {
    value1 = comparison;
}

void ExtraSubData::SetLogicalOperator(ExtraDataLogicalOperator logicalOperator) {
    value2 = logicalOperator;
}

void ExtraData::AddSubData(const ExtraSubData &data) {
    subData.push_back(data);
}

void BehaviorData::UpdateRuntimeState(CKBehavior *behavior) {
    runtimeBehaviorFlags = behavior ? static_cast<CKDWORD>(behavior->GetFlags()) : 0;
    runtimeInputCount = behavior ? behavior->GetInputCount() : -1;
    runtimeOutputCount = behavior ? behavior->GetOutputCount() : -1;
    runtimeHasTargetParameter = behavior && behavior->GetTargetParameter();
    isUsingTarget = behavior && behavior->IsUsingTarget();
}

void BehaviorData::AddLink(const Link &link) {
    links.push_back(link);
}

void BehaviorData::AddOperation(const Operation &operation) {
    operations.push_back(operation);
}

void BehaviorData::AddLocalParameter(const Parameter &parameter) {
    localParams.push_back(parameter);
}

void BehaviorData::AddSharedParameter(const Parameter &parameter) {
    sharedParams.push_back(parameter);
}

void BehaviorData::AddComment(const Comment &comment) {
    comments.push_back(comment);
}

Link *BehaviorData::FindLink(CK_ID linkId) {
    const auto found = std::find_if(
        links.begin(), links.end(),
        [linkId](const Link &link) { return link.id == linkId; });
    return found == links.end() ? nullptr : &*found;
}

std::vector<Link *> BehaviorData::FindLinksConnectedTo(CK_ID objectId) {
    std::vector<Link *> result;
    for (Link &link : links) {
        if (link.start.id == objectId || link.end.id == objectId) {
            result.push_back(&link);
        }
    }
    return result;
}

Operation *BehaviorData::FindOperation(CK_ID operationId) {
    const auto found = std::find_if(
        operations.begin(), operations.end(),
        [operationId](const Operation &operation) {
            return operation.id == operationId;
        });
    return found == operations.end() ? nullptr : &*found;
}

Parameter *BehaviorData::FindLocalParameter(CK_ID parameterId) {
    const auto found = std::find_if(
        localParams.begin(), localParams.end(),
        [parameterId](const Parameter &parameter) {
            return parameter.id == parameterId;
        });
    return found == localParams.end() ? nullptr : &*found;
}

Parameter *BehaviorData::FindSharedParameter(CK_ID parameterId) {
    const auto found = std::find_if(
        sharedParams.begin(), sharedParams.end(),
        [parameterId](const Parameter &parameter) {
            return parameter.id == parameterId;
        });
    return found == sharedParams.end() ? nullptr : &*found;
}

Parameter *BehaviorData::FindParameter(CK_ID parameterId) {
    Parameter *parameter = FindLocalParameter(parameterId);
    return parameter ? parameter : FindSharedParameter(parameterId);
}

Comment *BehaviorData::FindComment(CK_ID commentId) {
    const auto found = std::find_if(
        comments.begin(), comments.end(),
        [commentId](const Comment &comment) { return comment.id == commentId; });
    return found == comments.end() ? nullptr : &*found;
}

std::vector<InterfaceElement *> BehaviorData::FindElementsAt(
    const Point &position,
    float tolerance) {
    constexpr float parameterGridSpacing = 20.0f;
    std::vector<InterfaceElement *> result;

    if (rect.Contains(position)) result.push_back(this);
    for (Operation &operation : operations) {
        if (Point(operation.hPos, operation.vPos).DistanceTo(position) <= tolerance) {
            result.push_back(&operation);
        }
    }
    for (Parameter &parameter : localParams) {
        const Point pixelPosition(
            static_cast<float>(parameter.hPos) * parameterGridSpacing,
            static_cast<float>(parameter.vPos) * parameterGridSpacing);
        if (pixelPosition.DistanceTo(position) <= tolerance) {
            result.push_back(&parameter);
        }
    }
    for (Parameter &parameter : sharedParams) {
        const Point pixelPosition(
            static_cast<float>(parameter.hPos) * parameterGridSpacing,
            static_cast<float>(parameter.vPos) * parameterGridSpacing);
        if (pixelPosition.DistanceTo(position) <= tolerance) {
            result.push_back(&parameter);
        }
    }
    for (Comment &comment : comments) {
        if (comment.GetRect().Contains(position)) result.push_back(&comment);
    }
    for (Link &link : links) {
        if (link.PassesNear(position, tolerance)) result.push_back(&link);
    }
    return result;
}

namespace {
std::vector<BehaviorIOPosition> GetParameterPositions(const std::vector<int> &columns,
                                             const std::vector<int> &rows) {
    std::vector<BehaviorIOPosition> positions;
    positions.reserve(columns.size());
    for (size_t i = 0; i < columns.size(); ++i)
        positions.emplace_back(columns[i], i < rows.size() ? rows[i] : 0);
    return positions;
}

void SetParameterPositions(const std::vector<BehaviorIOPosition> &positions,
                    std::vector<int> &columns, std::vector<int> &rows) {
    columns.clear();
    rows.clear();
    for (const auto &position : positions) {
        columns.push_back(position.hPos);
        rows.push_back(position.vPos);
    }
}
} // namespace

std::vector<BehaviorIOPosition> BehaviorData::GetBehaviorInputPositions(bool foldedState) const {
    return foldedState ? GetParameterPositions(outwardInputs, foldedInputRows)
                       : GetParameterPositions(inwardInputs, expandedInputRows);
}

std::vector<BehaviorIOPosition> BehaviorData::GetBehaviorOutputPositions(bool foldedState) const {
    return foldedState ? GetParameterPositions(outwardOutputs, foldedOutputRows)
                       : GetParameterPositions(inwardOutputs, expandedOutputRows);
}

void BehaviorData::SetBehaviorInputPositions(bool foldedState, const std::vector<BehaviorIOPosition> &positions) {
    if (foldedState) SetParameterPositions(positions, outwardInputs, foldedInputRows);
    else SetParameterPositions(positions, inwardInputs, expandedInputRows);
}

void BehaviorData::SetBehaviorOutputPositions(bool foldedState, const std::vector<BehaviorIOPosition> &positions) {
    if (foldedState) SetParameterPositions(positions, outwardOutputs, foldedOutputRows);
    else SetParameterPositions(positions, inwardOutputs, expandedOutputRows);
}

void BehaviorData::Reset() {
    folded = false;
    flags = 0;
    UpdateRuntimeState(nullptr);
    isBehaviorGraph = false;
    depth = 0;
    parentId = 0;
    runtimeSiblingIndex = -1;
    rect = Rect();
    hExpandSize = 0.0f;
    vExpandSize = 0.0f;
    sectionedElementId = 0;
    sectionedParentElementId = 0;
    sectionedState = 0;
    sectionedSnapshot = nullptr;
    sectionedSnapshotStorage.reset();
    hasSectionedSections = false;
    sectionedSections.clear();
    sectionedScriptFlag = 0;
    links.clear();
    operations.clear();
    comments.clear();
    localParams.clear();
    sharedParams.clear();
    inwardInputs.clear();
    outwardInputs.clear();
    inwardOutputs.clear();
    outwardOutputs.clear();
    expandedInputRows.clear();
    foldedInputRows.clear();
    expandedOutputRows.clear();
    foldedOutputRows.clear();
}

void BehaviorData::SetSectionedSnapshot(void *bitmap) {
    if (sectionedSnapshotStorage)
        sectionedSnapshotStorage = sectionedSnapshotStorage->WithoutEncoding(bitmap);
    sectionedSnapshot = bitmap;
}

std::vector<InterfaceElement *> BehaviorData::GetAllElements() {
    std::vector<InterfaceElement *> result;
    result.push_back(this);
    for (Link &link : links) result.push_back(&link);
    for (Operation &operation : operations) result.push_back(&operation);
    for (Comment &comment : comments) result.push_back(&comment);
    for (Parameter &parameter : localParams) result.push_back(&parameter);
    for (Parameter &parameter : sharedParams) result.push_back(&parameter);
    return result;
}

bool BehaviorData::RemoveElement(InterfaceElement *element) {
    if (!element) return false;

    if (Link *link = dynamic_cast<Link *>(element)) {
        const auto found = std::find_if(
            links.begin(), links.end(),
            [link](const Link &candidate) { return candidate.id == link->id; });
        if (found == links.end()) return false;
        links.erase(found);
        return true;
    }
    if (Operation *operation = dynamic_cast<Operation *>(element)) {
        const auto found = std::find_if(
            operations.begin(), operations.end(),
            [operation](const Operation &candidate) {
                return candidate.id == operation->id;
            });
        if (found == operations.end()) return false;
        operations.erase(found);
        return true;
    }
    if (Comment *comment = dynamic_cast<Comment *>(element)) {
        const auto found = std::find_if(
            comments.begin(), comments.end(),
            [comment](const Comment &candidate) {
                return candidate.id == comment->id;
            });
        if (found == comments.end()) return false;
        comments.erase(found);
        return true;
    }
    if (Parameter *parameter = dynamic_cast<Parameter *>(element)) {
        auto local = std::find_if(
            localParams.begin(), localParams.end(),
            [parameter](const Parameter &candidate) {
                return candidate.id == parameter->id;
            });
        if (local != localParams.end()) {
            localParams.erase(local);
            return true;
        }
        auto shared = std::find_if(
            sharedParams.begin(), sharedParams.end(),
            [parameter](const Parameter &candidate) {
                return candidate.id == parameter->id;
            });
        if (shared != sharedParams.end()) {
            sharedParams.erase(shared);
            return true;
        }
    }
    return false;
}

// InterfaceData implementations
InterfaceData::InterfaceData() {
    // Initialize any required data
    Clear();
}

InterfaceData::~InterfaceData() {
    // Clean up resources
    Clear();
}

void InterfaceData::RefreshBehaviorCount() {
    behaviorCount = static_cast<int>(behaviors.size());
}

BehaviorData &InterfaceData::NewBehavior() {
    behaviors.emplace_back(0);
    RefreshBehaviorCount();
    BehaviorData &behavior = behaviors.back();
    NotifyObservers(&behavior, ElementAction::Added);
    return behavior;
}

void InterfaceData::AddBehavior(BehaviorData &behavior) {
    behaviors.push_back(behavior);
    RefreshBehaviorCount();
    NotifyObservers(&behavior, ElementAction::Added);
}

bool InterfaceData::RemoveBehavior(CK_ID behaviorId) {
    const auto found = std::find_if(
        behaviors.begin(), behaviors.end(),
        [behaviorId](const BehaviorData &behavior) {
            return behavior.id == behaviorId;
        });
    if (found == behaviors.end()) return false;
    NotifyObservers(&*found, ElementAction::Removed);
    behaviors.erase(found);
    RefreshBehaviorCount();
    return true;
}

BehaviorData *InterfaceData::FindBehavior(CK_ID id) {
    if (rootBehavior.id == id) {
        return &rootBehavior;
    }

    const auto it = std::find_if(behaviors.begin(), behaviors.end(),
                           [id](const BehaviorData &behavior) { return behavior.id == id; });
    return it != behaviors.end() ? &(*it) : nullptr;
}

Operation * InterfaceData::FindOperation(CK_ID id) {
    // Check root behavior operations
    for (auto &op : rootBehavior.operations) {
        if (op.id == id) {
            return &op;
        }
    }

    // Check all other behaviors
    for (auto &behavior : behaviors) {
        for (auto &op : behavior.operations) {
            if (op.id == id) {
                return &op;
            }
        }
    }

    return nullptr;
}

void InterfaceData::AddExtraData(const ExtraData &data) {
    extraData.push_back(data);
}

void InterfaceData::Clear() {
    sectionStorage.reset();
    version = Format::CURRENT_VERSION;
    versionIdentifier = Format::LEGACY_TOP_LEVEL_MARKER;
    useDevSectionedLayout = false;
    generatedFromRuntimeGraph = false;
    header = Header();
    rootBehavior.Reset();
    behaviors.clear();
    RefreshBehaviorCount();
    extraDataVersion = 0;
    extraData.clear();
}

std::vector<CKDWORD> InterfaceData::GetUnparsedSectionIdentifiers() const {
    return sectionStorage ? sectionStorage->UnparsedIdentifiers() : std::vector<CKDWORD>();
}

bool InterfaceData::RemoveUnparsedSection(CKDWORD identifier) {
    if (!sectionStorage) return false;
    if (!sectionStorage.unique()) {
        auto edited = std::make_shared<InterfaceChunkSections>(*sectionStorage);
        if (!edited->RemoveUnparsed(identifier)) return false;
        sectionStorage = edited;
        return true;
    }
    return sectionStorage->RemoveUnparsed(identifier);
}

void InterfaceData::AddObserver(ElementObserver *observer) {
    if (observer &&
        std::find(observers.begin(), observers.end(), observer) == observers.end()) {
        observers.push_back(observer);
    }
}

void InterfaceData::RemoveObserver(ElementObserver *observer) {
    const auto found = std::find(observers.begin(), observers.end(), observer);
    if (found != observers.end()) observers.erase(found);
}

void InterfaceData::NotifyObservers(
    InterfaceElement *element,
    ElementAction action) const {
    for (ElementObserver *observer : observers) {
        switch (action) {
        case ElementAction::Added:
            observer->OnElementAdded(element);
            break;
        case ElementAction::Removed:
            observer->OnElementRemoved(element);
            break;
        case ElementAction::Modified:
            observer->OnElementModified(element);
            break;
        }
    }
}

std::vector<InterfaceElement *> InterfaceData::FindElementsById(CK_ID id) {
    std::vector<InterfaceElement *> result;
    if (rootBehavior.id == id) result.push_back(&rootBehavior);
    if (header.id == id) result.push_back(&header);

    const auto collect = [id, &result](BehaviorData &behavior) {
        if (behavior.id == id) result.push_back(&behavior);
        for (Link &link : behavior.links) {
            if (link.id == id) result.push_back(&link);
        }
        for (Operation &operation : behavior.operations) {
            if (operation.id == id) result.push_back(&operation);
        }
        for (Comment &comment : behavior.comments) {
            if (comment.id == id) result.push_back(&comment);
        }
        for (Parameter &parameter : behavior.localParams) {
            if (parameter.id == id) result.push_back(&parameter);
        }
        for (Parameter &parameter : behavior.sharedParams) {
            if (parameter.id == id) result.push_back(&parameter);
        }
    };

    for (BehaviorData &behavior : behaviors) collect(behavior);
    return result;
}

std::vector<InterfaceElement *> InterfaceData::FindElementsAt(
    const Point &position,
    float tolerance) {
    std::vector<InterfaceElement *> result =
        rootBehavior.FindElementsAt(position, tolerance);
    for (BehaviorData &behavior : behaviors) {
        std::vector<InterfaceElement *> elements =
            behavior.FindElementsAt(position, tolerance);
        result.insert(result.end(), elements.begin(), elements.end());
    }
    return result;
}

std::vector<Link *> InterfaceData::FindLinksConnectedTo(CK_ID objectId) {
    std::vector<Link *> result = rootBehavior.FindLinksConnectedTo(objectId);
    for (BehaviorData &behavior : behaviors) {
        std::vector<Link *> links = behavior.FindLinksConnectedTo(objectId);
        result.insert(result.end(), links.begin(), links.end());
    }
    return result;
}

std::vector<BehaviorData *> InterfaceData::GetSubTree(CK_ID rootId) {
    std::vector<BehaviorData *> result;
    BehaviorData *root = FindBehavior(rootId);
    if (!root) return result;

    std::set<CK_ID> visited;
    std::queue<CK_ID> pending;
    visited.insert(rootId);
    pending.push(rootId);
    result.push_back(root);

    while (!pending.empty()) {
        const CK_ID currentId = pending.front();
        pending.pop();
        for (Link *link : FindLinksConnectedTo(currentId)) {
            if (!link->end.IsBehaviorRelated() ||
                !visited.insert(link->end.id).second) {
                continue;
            }
            BehaviorData *behavior = FindBehavior(link->end.id);
            if (behavior) {
                result.push_back(behavior);
                pending.push(link->end.id);
            }
        }
    }
    return result;
}

BehaviorData *InterfaceData::FindParentBehavior(CK_ID behaviorId) {
    for (Link *link : FindLinksConnectedTo(behaviorId)) {
        if (link->IsBehaviorLink() && link->end.id == behaviorId) {
            return FindBehavior(link->start.id);
        }
    }
    return nullptr;
}

std::vector<BehaviorData *> InterfaceData::FindChildBehaviors(CK_ID behaviorId) {
    std::vector<BehaviorData *> result;
    for (Link *link : FindLinksConnectedTo(behaviorId)) {
        if (!link->IsBehaviorLink() || link->start.id != behaviorId) continue;
        BehaviorData *behavior = FindBehavior(link->end.id);
        if (behavior) result.push_back(behavior);
    }
    return result;
}

bool InterfaceData::HasPath(CK_ID startId, CK_ID endId) {
    return !FindPath(startId, endId).empty();
}

std::vector<CK_ID> InterfaceData::FindPath(CK_ID startId, CK_ID endId) {
    std::map<CK_ID, CK_ID> predecessor;
    std::set<CK_ID> visited;
    std::queue<CK_ID> pending;
    visited.insert(startId);
    pending.push(startId);

    while (!pending.empty()) {
        const CK_ID currentId = pending.front();
        pending.pop();
        if (currentId == endId) break;
        for (Link *link : FindLinksConnectedTo(currentId)) {
            if (!link->IsBehaviorLink() || link->start.id != currentId) continue;
            if (visited.insert(link->end.id).second) {
                predecessor[link->end.id] = currentId;
                pending.push(link->end.id);
            }
        }
    }

    if (visited.find(endId) == visited.end()) return {};
    std::vector<CK_ID> result;
    for (CK_ID currentId = endId;; currentId = predecessor[currentId]) {
        result.push_back(currentId);
        if (currentId == startId) break;
    }
    std::reverse(result.begin(), result.end());
    return result;
}
