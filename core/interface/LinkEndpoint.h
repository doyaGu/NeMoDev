#pragma once
#include <cstdint>

/**
 * @enum EndpointType
 * @brief Defines the types of endpoints for links
 */
enum EndpointType {
    ENDPOINT_POUT_SHORTCUT = 5, ///< Parameter output link shortcut
    ENDPOINT_PIN = 7,           ///< Parameter input
    ENDPOINT_POUT = 8,          ///< Parameter output
    ENDPOINT_PLOCAL = 9,        ///< Local parameter
    ENDPOINT_TARGET_PIN = 10,   ///< Target parameter input
    ENDPOINT_BIN = 12,          ///< Behavior input
    ENDPOINT_BOUT = 13,         ///< Behavior output
    ENDPOINT_START_BIN = 26     ///< "Start" behavior input
};

/**
 * @enum ParameterStyle
 * @brief Defines the display style for parameters
 */
enum ParameterStyle : int {
    PARAM_STYLE_NAME = 0x200,       ///< Display parameter name only
    PARAM_STYLE_COLLAPSED = 0x400,  ///< Parameter is closed/collapsed
    PARAM_STYLE_NAMEVALUE = 0x1000, ///< Display parameter name and value
    PARAM_STYLE_VALUE = 0x2000      ///< Display parameter value only
};

/**
 * @struct InterfaceElement
 * @brief Base class for all interface elements with common functionality
 */
struct InterfaceElement {
    uint32_t id = 0; ///< ID of the element

    InterfaceElement() = default;

    explicit InterfaceElement(uint32_t elementId) : id(elementId) {}

    virtual ~InterfaceElement() = default;
};

/**
 * @struct LinkEndpoint
 * @brief Represents an endpoint of a link in the behavior tree
 */
struct LinkEndpoint : InterfaceElement {
    int index = 0;                                    ///< Index of the endpoint within its owner
    EndpointType type = static_cast<EndpointType>(0); ///< Type of the endpoint
    /// For payload 0x05..0x10, type holds an unmapped stored code, not EndpointType.
    /// Set explicitly when constructing old wire records; not a conversion switch.
    bool hasLegacyTypeCode = false;

    LinkEndpoint() = default;

    LinkEndpoint(uint32_t endpointId, int endpointIndex, EndpointType endpointType)
        : InterfaceElement(endpointId), index(endpointIndex), type(endpointType) {}

    /**
     * @brief Convenience constructor from raw type value
     */
    LinkEndpoint(uint32_t endpointId, int endpointIndex, int endpointType)
        : InterfaceElement(endpointId), index(endpointIndex), type(static_cast<EndpointType>(endpointType)) {}

    /**
     * @brief Checks if this endpoint is a parameter input
     */
    bool IsParameterInput() const;

    /**
     * @brief Checks if this endpoint is a parameter output
     */
    bool IsParameterOutput() const;

    /**
     * @brief Checks if this endpoint is a parameter output shortcut
     */
    bool IsParameterOutputShortcut() const;

    /** @deprecated Use IsParameterOutputShortcut. */
    bool IsParameterShortCut() const { return IsParameterOutputShortcut(); }

    /**
     * @brief Checks if this endpoint is a local parameter
     */
    bool IsParameterLocal() const;

    /**
     * @brief Checks if this endpoint is a behavior input
     */
    bool IsBehaviorInput() const;

    bool IsStartBehaviorInput() const;

    /**
     * @brief Checks if this endpoint is a behavior output
     */
    bool IsBehaviorOutput() const;

    bool IsParameterRelated() const;
    bool IsBehaviorRelated() const;
    bool IsCompatibleWith(const LinkEndpoint &other) const;
};
