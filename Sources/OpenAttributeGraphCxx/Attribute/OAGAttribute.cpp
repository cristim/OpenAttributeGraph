//
//  OAGAttribute.cpp
//  OpenAttributeGraphCxx

#include <OpenAttributeGraph/OAGAttribute.h>
#include <OpenAttributeGraph/OAGSubgraph.h>
#include <OpenAttributeGraphCxx/Attribute/AttributeID.hpp>
#include <OpenAttributeGraphCxx/Graph/Graph.hpp>
#include <OpenAttributeGraphCxx/Misc/assert.hpp>
#include <OpenAttributeGraphCxx/Runtime/metadata.hpp>
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <pthread.h>
#include <vector>

const OAGAttribute OAGAttributeNil = OAGAttribute(OAG::AttributeID::Kind::Null);

extern "C" const void *OAGSwiftRuleValueMetadata(long index);
extern "C" const void *OAGSwiftRuleBodyMetadata(long index);
extern "C" void OAGSwiftUpdateRule(long index, void *body, OAGAttribute attribute);

namespace {
thread_local OAGAttribute updatingAttribute = OAGAttributeNil;
}

OAGAttribute OAGGraphGetCurrentAttribute(void) {
    return updatingAttribute;
}

bool OAGGraphCurrentAttributeWasModified(void) {
    // TODO
    return false;
}

namespace {
struct ObservedGeneration {
    OAGAttribute owner;
    uint64_t generation;
    ObservedGeneration *next;
};

struct StoredAttribute {
    OAGAttribute id;
    const OAG::swift::metadata *type;
    void *storage;
    bool hasValue;
    long ruleIndex;
    void *body;
    const OAG::swift::metadata *bodyType;
    bool updating;
    OAGAttribute parent;
    OAGAttribute dependency;
    uint64_t offset;
    uint64_t generation;
    ObservedGeneration *observers;
    OAGSubgraphRef subgraph;
    OAGGraphRef graph;
    StoredAttribute *next;
};

pthread_mutex_t attributeLock = PTHREAD_MUTEX_INITIALIZER;
StoredAttribute *attributes = nullptr;
uint32_t nextAttributeID = 4;

StoredAttribute *findAttribute(OAGAttribute id) {
    for (auto *entry = attributes; entry != nullptr; entry = entry->next) {
        if (entry->id == id) return entry;
    }
    return nullptr;
}

// Called with attributeLock held. A changed input makes every cached rule that
// observed it stale, including rules that depend on those rules transitively.
void invalidateObservers(StoredAttribute *source) {
    std::vector<OAGAttribute> pending{source->id};
    std::vector<OAGAttribute> visited;
    while (!pending.empty()) {
        const OAGAttribute id = pending.back();
        pending.pop_back();
        if (std::find(visited.begin(), visited.end(), id) != visited.end()) continue;
        visited.push_back(id);
        auto *input = findAttribute(id);
        if (input == nullptr) continue;
        for (auto *observer = input->observers; observer != nullptr; observer = observer->next) {
            auto *rule = findAttribute(observer->owner);
            if (rule == nullptr || rule->updating) continue;
            if (rule->hasValue) {
                rule->type->vw_destroy(reinterpret_cast<swift::OpaqueValue *>(rule->storage));
                rule->hasValue = false;
            }
            pending.push_back(rule->id);
        }
    }
}

OAGAttribute create_offset_attribute(OAGAttribute attribute, uint64_t offset, std::optional<uint64_t> size) {
    pthread_mutex_lock(&attributeLock);
    if (findAttribute(attribute) == nullptr) {
        pthread_mutex_unlock(&attributeLock);
        return OAGAttributeNil;
    }
    auto *entry = new StoredAttribute{nextAttributeID, nullptr, nullptr, false, 0,
                                      nullptr, nullptr, false, attribute, OAGAttributeNil, offset,
                                      0, nullptr, nullptr, nullptr, attributes};
    nextAttributeID += 4;
    attributes = entry;
    pthread_mutex_unlock(&attributeLock);
    return entry->id;
}
}

OAGAttribute OAGGraphCreateOffsetAttribute(OAGAttribute attribute, long offset) {
    return create_offset_attribute(attribute, offset, std::nullopt);
}

OAGAttribute OAGGraphCreateOffsetAttribute2(OAGAttribute attribute, long offset, uint64_t size) {
    return create_offset_attribute(attribute, offset, std::optional(size));
}

namespace {
OAGAttribute create_indirect_attribute(OAGAttribute attribute, std::optional<uint64_t> size) {
    return create_offset_attribute(attribute, 0, size);
}
}

OAGAttribute OAGGraphCreateIndirectAttribute(OAGAttribute attribute) {
    return create_indirect_attribute(attribute, std::nullopt);
}

OAGAttribute OAGGraphCreateIndirectAttribute2(OAGAttribute attribute, uint64_t size) {
    return create_indirect_attribute(attribute, std::optional(size));
}

OAGAttributeFlags OAGGraphGetFlags(OAGAttribute attribute) {
    const OAG::AttributeID id = OAG::AttributeID(attribute);
    id.checkIsDirect();
    // TODO: data/table
    return OAGAttributeFlagsNone;
}

void OAGGraphSetFlags(OAGAttribute attribute, OAGAttributeFlags flags) {
    const OAG::AttributeID id = OAG::AttributeID(attribute);
    id.checkIsDirect();
    // TODO: data/table
}

void OAGGraphAddInput(OAGAttribute attribute1, OAGAttribute attribute2, OAGInputOptions options, long token) {
    const OAG::AttributeID id = OAG::AttributeID(attribute1);
    id.checkIsDirect();
    // TODO: data/table
}

const OAGAttributeInfo OAGGraphGetAttributeInfo(OAGAttribute attribute) {
    const OAG::AttributeID id = OAG::AttributeID(attribute);
    id.checkIsDirect();
    // TODO
    return { nullptr, nullptr };
}

void OAGGraphMutateAttribute(OAGAttribute attribute,
                            const OAGTypeID type,
                            bool invalidating,
                            const void (*function)(const void * _Nullable context OAG_SWIFT_CONTEXT, void *body) OAG_SWIFT_CC(swift),
                            const void * _Nullable context) {
    pthread_mutex_lock(&attributeLock);
    auto *entry = findAttribute(attribute);
    if (entry == nullptr || entry->body == nullptr ||
        reinterpret_cast<OAGTypeID>(entry->bodyType) != type) {
        pthread_mutex_unlock(&attributeLock);
        return;
    }
    // The callback may access other attributes, so release the registry lock.
    pthread_mutex_unlock(&attributeLock);
    function(context, entry->body);
    if (invalidating) {
        pthread_mutex_lock(&attributeLock);
        if (entry->hasValue) {
            entry->type->vw_destroy(reinterpret_cast<swift::OpaqueValue *>(entry->storage));
            entry->hasValue = false;
        }
        invalidateObservers(entry);
        const OAGGraphRef graph = entry->graph;
        pthread_mutex_unlock(&attributeLock);
        if (graph != nullptr && updatingAttribute == OAGAttributeNil) {
            graph->context.notify_invalidation(attribute);
        }
    }
}

OAGAttribute OAGGraphGetIndirectDependency(OAGAttribute attribute) {
    pthread_mutex_lock(&attributeLock);
    auto *entry = findAttribute(attribute);
    const OAGAttribute dependency = entry != nullptr ? entry->dependency : OAGAttributeNil;
    pthread_mutex_unlock(&attributeLock);
    return dependency;
}

void OAGGraphSetIndirectDependency(OAGAttribute attribute1, OAGAttribute attribute2) {
    pthread_mutex_lock(&attributeLock);
    auto *entry = findAttribute(attribute1);
    if (entry != nullptr) entry->dependency = attribute2;
    pthread_mutex_unlock(&attributeLock);
}

OAGAttribute OAGGraphGetIndirectAttribute(OAGAttribute attribute) {
    pthread_mutex_lock(&attributeLock);
    auto *entry = findAttribute(attribute);
    const OAGAttribute source = entry != nullptr ? entry->parent : OAGAttributeNil;
    pthread_mutex_unlock(&attributeLock);
    return source;
}

void OAGGraphSetIndirectAttribute(OAGAttribute attribute1, OAGAttribute attribute2) {
    pthread_mutex_lock(&attributeLock);
    auto *entry = findAttribute(attribute1);
    if (entry != nullptr && entry->storage == nullptr) entry->parent = attribute2;
    pthread_mutex_unlock(&attributeLock);
}

OAGAttribute OAGGraphCreateAttribute(long index, const void *body, const void * value) {
    const bool isRule = (index & 1) != 0;
    auto *type = reinterpret_cast<const OAG::swift::metadata *>(
        isRule ? OAGSwiftRuleValueMetadata(index) : reinterpret_cast<const void *>(index));
    if (type == nullptr) return OAGAttributeNil;
    const auto *witnesses = type->getValueWitnesses();
    const size_t alignment = std::max<size_t>(witnesses->getAlignment(), sizeof(void *));
    const size_t size = std::max<size_t>(witnesses->getStride(), 1);
    void *storage = nullptr;
    if (posix_memalign(&storage, alignment, size) != 0) {
        OAG::precondition_failure("attribute allocation failure");
    }
    if (value != nullptr) {
        type->vw_initializeWithCopy(reinterpret_cast<swift::OpaqueValue *>(storage),
                                    reinterpret_cast<swift::OpaqueValue *>(const_cast<void *>(value)));
    }
    void *bodyStorage = nullptr;
    const OAG::swift::metadata *bodyType = nullptr;
    if (isRule && body != nullptr) {
        bodyType = reinterpret_cast<const OAG::swift::metadata *>(OAGSwiftRuleBodyMetadata(index));
        const auto *bodyWitnesses = bodyType->getValueWitnesses();
        const size_t bodyAlignment = std::max<size_t>(bodyWitnesses->getAlignment(), sizeof(void *));
        const size_t bodySize = std::max<size_t>(bodyWitnesses->getStride(), 1);
        if (posix_memalign(&bodyStorage, bodyAlignment, bodySize) != 0) {
            OAG::precondition_failure("attribute body allocation failure");
        }
        bodyType->vw_initializeWithCopy(reinterpret_cast<swift::OpaqueValue *>(bodyStorage),
                                         reinterpret_cast<swift::OpaqueValue *>(const_cast<void *>(body)));
    }
    OAGSubgraphRef subgraph = OAGSubgraphGetCurrent();
    OAGGraphRef graph = subgraph != nullptr ? OAGSubgraphGetGraph(subgraph) : nullptr;
    auto *entry = new StoredAttribute{0, type, storage, value != nullptr, isRule ? index : 0,
                                      bodyStorage, bodyType, false, OAGAttributeNil, OAGAttributeNil, 0,
                                      value != nullptr ? 1u : 0u, nullptr, subgraph, graph, nullptr};
    pthread_mutex_lock(&attributeLock);
    entry->id = nextAttributeID;
    nextAttributeID += 4;
    entry->next = attributes;
    attributes = entry;
    pthread_mutex_unlock(&attributeLock);
    return entry->id;
}

const OAGValue OAGGraphGetValue(OAGAttribute attribute, OAGValueOptions options, OAGTypeID type) {
    pthread_mutex_lock(&attributeLock);
    auto *entry = findAttribute(attribute);
    const OAGAttribute parent = entry != nullptr ? entry->parent : OAGAttributeNil;
    const uint64_t offset = entry != nullptr ? entry->offset : 0;
    if (parent != OAGAttributeNil) {
        auto *parentEntry = findAttribute(parent);
        const OAGTypeID parentType = parentEntry != nullptr ? reinterpret_cast<OAGTypeID>(parentEntry->type) : nullptr;
        pthread_mutex_unlock(&attributeLock);
        const OAGValue base = OAGGraphGetValue(parent, options, parentType);
        return {base.value != nullptr ? static_cast<const char *>(base.value) + offset : nullptr, base.flags};
    }
    const bool needsUpdate = entry != nullptr && !entry->hasValue &&
                             entry->ruleIndex != 0 && entry->body != nullptr && !entry->updating;
    if (needsUpdate) entry->updating = true;
    pthread_mutex_unlock(&attributeLock);
    if (needsUpdate) {
        const OAGAttribute previous = updatingAttribute;
        updatingAttribute = attribute;
        OAGSwiftUpdateRule(entry->ruleIndex, entry->body, attribute);
        updatingAttribute = previous;
        pthread_mutex_lock(&attributeLock);
        entry->updating = false;
        pthread_mutex_unlock(&attributeLock);
    }
    pthread_mutex_lock(&attributeLock);
    entry = findAttribute(attribute);
    // Attribute.unsafeBitCast intentionally reads a single-field wrapper using the
    // field's metadata (for example ReferenceDateModifier as Date?). Keep the
    // allocation bound even when the metadata identities differ.
    const auto *requestedType = reinterpret_cast<const OAG::swift::metadata *>(type);
    const bool compatibleType = entry != nullptr && requestedType != nullptr &&
        (entry->type == requestedType ||
         requestedType->getValueWitnesses()->getStride() <= entry->type->getValueWitnesses()->getStride());
    const void *value = entry != nullptr && entry->hasValue && compatibleType ? entry->storage : nullptr;
    OAGChangedValueFlags flags = static_cast<OAGChangedValueFlags>(0);
    if (value != nullptr && updatingAttribute != OAGAttributeNil && updatingAttribute != attribute) {
        auto **cursor = &entry->observers;
        while (*cursor != nullptr && (*cursor)->owner != updatingAttribute) {
            cursor = &(*cursor)->next;
        }
        if (*cursor == nullptr) {
            *cursor = new ObservedGeneration{updatingAttribute, 0, nullptr};
        }
        if ((*cursor)->generation != entry->generation) {
            (*cursor)->generation = entry->generation;
            flags = OAGChangedValueFlagsChanged;
        }
    }
    pthread_mutex_unlock(&attributeLock);
    return OAGValue { value, flags };
}

bool OAGGraphSetValue(OAGAttribute attribute, const void *value, OAGTypeID type) {
    pthread_mutex_lock(&attributeLock);
    auto *entry = findAttribute(attribute);
    const OAGAttribute parent = entry != nullptr ? entry->parent : OAGAttributeNil;
    if (parent != OAGAttributeNil) {
        pthread_mutex_unlock(&attributeLock);
        const OAGValue destination = OAGGraphGetValue(attribute, 0, type);
        if (destination.value == nullptr || value == nullptr) return false;
        auto *metadata = reinterpret_cast<const OAG::swift::metadata *>(type);
        metadata->vw_assignWithCopy(
            reinterpret_cast<swift::OpaqueValue *>(const_cast<void *>(destination.value)),
            reinterpret_cast<swift::OpaqueValue *>(const_cast<void *>(value)));
        return true;
    }
    if (entry == nullptr || reinterpret_cast<OAGTypeID>(entry->type) != type || value == nullptr) {
        pthread_mutex_unlock(&attributeLock);
        return false;
    }
    auto *destination = reinterpret_cast<swift::OpaqueValue *>(entry->storage);
    auto *source = reinterpret_cast<swift::OpaqueValue *>(const_cast<void *>(value));
    if (entry->hasValue) entry->type->vw_assignWithCopy(destination, source);
    else entry->type->vw_initializeWithCopy(destination, source);
    entry->hasValue = true;
    ++entry->generation;
    invalidateObservers(entry);
    const OAGGraphRef graph = entry->graph;
    pthread_mutex_unlock(&attributeLock);
    if (graph != nullptr && updatingAttribute == OAGAttributeNil) {
        graph->context.notify_invalidation(attribute);
    }
    return true;
}

const OAGValue OAGGraphGetInputValue(OAGAttribute attribute, OAGAttribute inputAttribute, OAGValueOptions options, const OAGTypeID type) {
    return OAGGraphGetValue(inputAttribute, options, type);
}

const void * _Nullable OAGGraphGetOutputValue(OAGTypeID type) {
    // TODO
    return nullptr;
}

void OAGGraphSetOutputValue(const void *value, const OAGTypeID type) {
    if (updatingAttribute != OAGAttributeNil) {
        OAGGraphSetValue(updatingAttribute, value, type);
    }
}

OAGValueState OAGGraphGetValueState(OAGAttribute attribute) {
    // TODO
    return {};
}

bool OAGGraphHasValue(OAGAttribute attribute) {
    pthread_mutex_lock(&attributeLock);
    auto *entry = findAttribute(attribute);
    const OAGAttribute parent = entry != nullptr ? entry->parent : OAGAttributeNil;
    if (parent != OAGAttributeNil) {
        pthread_mutex_unlock(&attributeLock);
        return OAGGraphHasValue(parent);
    }
    const bool hasValue = entry != nullptr && entry->hasValue;
    pthread_mutex_unlock(&attributeLock);
    return hasValue;
}

void OAGGraphUpdateValue(OAGAttribute attribute) {
    // TODO
}

void OAGGraphPrefetchValue(OAGAttribute attribute) {
    // TODO
}

void OAGGraphInvalidateValue(OAGAttribute attribute) {
    pthread_mutex_lock(&attributeLock);
    auto *entry = findAttribute(attribute);
    if (entry == nullptr) {
        pthread_mutex_unlock(&attributeLock);
        return;
    }
    if (entry->hasValue) {
        entry->type->vw_destroy(reinterpret_cast<swift::OpaqueValue *>(entry->storage));
        entry->hasValue = false;
    }
    invalidateObservers(entry);
    const OAGGraphRef graph = entry->graph;
    pthread_mutex_unlock(&attributeLock);
    if (graph != nullptr && updatingAttribute == OAGAttributeNil) {
        graph->context.notify_invalidation(attribute);
    }
}

void OAGGraphVerifyType(OAGAttribute attribute, OAGTypeID type) {
    // TODO
}

OAGGraphRef OAGGraphGetAttributeGraph(OAGAttribute attribute) {
    pthread_mutex_lock(&attributeLock);
    auto *entry = findAttribute(attribute);
    const OAGGraphRef graph = entry != nullptr ? entry->graph : nullptr;
    const OAGAttribute parent = entry != nullptr ? entry->parent : OAGAttributeNil;
    pthread_mutex_unlock(&attributeLock);
    return graph != nullptr || parent == OAGAttributeNil ? graph : OAGGraphGetAttributeGraph(parent);
}

OAGSubgraphRef OAGGraphGetAttributeSubgraph(OAGAttribute attribute) {
    OAGSubgraphRef subgraph = OAGGraphGetAttributeSubgraph2(attribute);
    if (subgraph == nullptr) {
        OAG::precondition_failure("no subgraph");
    }
    return subgraph;
}

_Nullable OAGSubgraphRef OAGGraphGetAttributeSubgraph2(OAGAttribute attribute) {
    pthread_mutex_lock(&attributeLock);
    auto *entry = findAttribute(attribute);
    const OAGSubgraphRef subgraph = entry != nullptr ? entry->subgraph : nullptr;
    const OAGAttribute parent = entry != nullptr ? entry->parent : OAGAttributeNil;
    pthread_mutex_unlock(&attributeLock);
    return subgraph != nullptr || parent == OAGAttributeNil ? subgraph : OAGGraphGetAttributeSubgraph2(parent);
}

const void * OAGGraphReadCachedAttribute(long hashValue, OAGTypeID bodyType, const void *bodyPointer, OAGTypeID valueType, OAGCachedValueOptions options, OAGAttribute attribute, bool unknown) {
    // TODO
    return nullptr;
}

const void * _Nullable OAGGraphReadCachedAttributeIfExists(long hashValue, OAGTypeID bodyType, const void *bodyPointer, OAGTypeID valueType, OAGCachedValueOptions options, OAGAttribute attribute, bool unknown) {
    // TODO
    return nullptr;
}

void OAGGraphWithUpdate(
    OAGAttribute attribute,
    void (* callback)(const void *context OAG_SWIFT_CONTEXT) OAG_SWIFT_CC(swift),
    const void *context
) {
    // TODO
    callback(context);
}
