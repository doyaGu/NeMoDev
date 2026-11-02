#pragma once

#include "core/graph/GraphActionRecord.h"
#include "core/graph/detail/TopologyInternal.h"

namespace vdev::detail {

GraphMutation GraphAttachSubBehavior(const Document &, CK_ID owner, CK_ID child, int slot);
GraphMutation GraphDetachSubBehavior(const Document &, CK_ID owner, CK_ID child);
GraphMutation GraphAttachLink(const Document &, CK_ID owner, CK_ID link, int slot, const IoRef &in, const IoRef &out);
GraphMutation GraphDetachLink(const Document &, CK_ID owner, CK_ID link);
GraphMutation GraphAttachOperation(const Document &, CK_ID owner, CK_ID operation, int slot);
GraphMutation GraphDetachOperation(const Document &, CK_ID owner, CK_ID operation);
GraphMutation GraphParameterList(const Document &, CK_ID owner, CK_ID parameter, int slot, GraphList list, bool insert);
GraphMutation GraphInsertIo(const Document &, CK_ID owner, bool input, int slot, CK_ID io);
GraphMutation GraphRemoveIo(const Document &, CK_ID owner, bool input, int slot, CK_ID io);
GraphMutation GraphSetSource(const Document &, const InputRef &input, CK_ID direct = 0, const InputRef &shared = {});
GraphMutation GraphRestoreSource(const Document &, const InputSource &source);
GraphMutation GraphAddDestination(const Document &, CK_ID output, CK_ID destination, bool copyValue);
GraphMutation GraphAppendDestinations(const Document &, CK_ID output, const std::vector<CK_ID> &destinations);
GraphMutation GraphRemoveDestination(const Document &, CK_ID output, CK_ID destination);
GraphMutation GraphRestoreDestinations(const Document &, CK_ID output, const std::vector<CK_ID> &destinations);
GraphMutation GraphWriteText(const Document &, CK_ID parameter, const std::string &text);
GraphMutation GraphRename(const Document &, CK_ID object, const std::string &name);
GraphMutation GraphSetInteger(const Document &, CK_ID object, GraphIntegerProperty property, int value);
GraphMutation GraphMoveLocal(const Document &, CK_ID from, int fromSlot, CK_ID to, int toSlot, CK_ID parameter);
GraphMutation GraphSequence(std::initializer_list<GraphMutation> changes);

} // namespace vdev::detail
