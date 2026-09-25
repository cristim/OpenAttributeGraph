//
//  OAGWeakAttribute.cpp
//  OpenAttributeGraphCxx

#include <OpenAttributeGraph/OAGWeakAttribute.h>

OAGWeakAttribute OAGCreateWeakAttribute(OAGAttribute attribute) {
    return {{attribute == 0 ? OAGAttributeNil : attribute, 0}};
}

OAGAttribute OAGWeakAttributeGetAttribute(OAGWeakAttribute weakAttribute) {
    // Swift's default WeakAttribute() uses raw identifier zero as an empty
    // sentinel, while AnyAttribute.nil has its own encoded value.
    return weakAttribute._details.identifier == 0 ? OAGAttributeNil : weakAttribute._details.identifier;
}

OAGWeakValue OAGGraphGetWeakValue(OAGWeakAttribute weakAttribute, OAGValueOptions options, OAGTypeID type) {
    const OAGValue value = OAGGraphGetValue(weakAttribute._details.identifier, options, type);
    return {value.value, value.flags};
}
