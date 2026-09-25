//
//  OAGWeakAttribute.cpp
//  OpenAttributeGraphCxx

#include <OpenAttributeGraph/OAGWeakAttribute.h>

OAGWeakAttribute OAGCreateWeakAttribute(OAGAttribute attribute) {
    return {{attribute, 0}};
}

OAGAttribute OAGWeakAttributeGetAttribute(OAGWeakAttribute weakAttribute) {
    return weakAttribute._details.identifier;
}

OAGWeakValue OAGGraphGetWeakValue(OAGWeakAttribute weakAttribute, OAGValueOptions options, OAGTypeID type) {
    const OAGValue value = OAGGraphGetValue(weakAttribute._details.identifier, options, type);
    return {value.value, value.flags};
}
