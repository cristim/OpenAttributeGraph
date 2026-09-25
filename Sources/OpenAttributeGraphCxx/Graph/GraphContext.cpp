//
//  GraphContext.cpp
//  OpenAttributeGraphCxx

#include <OpenAttributeGraphCxx/Graph/Graph.hpp>
#include <OpenAttributeGraph/OAGGraph.h>
#include <OpenAttributeGraphCxx/Misc/assert.hpp>

extern "C" void *swift_retain(void *object);
extern "C" void swift_release(void *object);

OAG::Graph::Context &OAG::Graph::Context::from_cf(OAGGraphRef storage) OAG_NOEXCEPT {
    if (storage->context.isInvalid()) {
        OAG::precondition_failure("invalidated graph");
    }
    return storage->context;
}

OAG::Graph::Context::Context(OAG::Graph &graph) OAG_NOEXCEPT :
_graph(&graph),
_context(nullptr),
_id(OAGMakeUniqueID()),
_invalidation_callback(nullptr),
_update_callback(nullptr),
unknown1(0),
unknown2(0),
unknown3(0),
_isInvalid(false) {
    // TODO
}

OAG::Graph::Context::~Context() OAG_NOEXCEPT {
    if (_invalidation_callback.context() != nullptr) {
        swift_release(const_cast<void *>(_invalidation_callback.context()));
    }
    if (_update_callback.context() != nullptr) {
        swift_release(const_cast<void *>(_update_callback.context()));
    }
}

void OAG::Graph::Context::set_invalidation_callback(
    ClosureFunction<void, OAGAttribute> callback) OAG_NOEXCEPT {
    if (callback.context() != nullptr) {
        swift_retain(const_cast<void *>(callback.context()));
    }
    const void *old = _invalidation_callback.context();
    _invalidation_callback = callback;
    if (old != nullptr) swift_release(const_cast<void *>(old));
}

void OAG::Graph::Context::set_update_callback(ClosureFunction<void> callback) OAG_NOEXCEPT {
    if (callback.context() != nullptr) {
        swift_retain(const_cast<void *>(callback.context()));
    }
    const void *old = _update_callback.context();
    _update_callback = callback;
    if (old != nullptr) swift_release(const_cast<void *>(old));
}

const bool OAG::Graph::Context::thread_is_updating() const OAG_NOEXCEPT {
    return _graph->thread_is_updating() && _graph->is_context_updating(*this);
}
