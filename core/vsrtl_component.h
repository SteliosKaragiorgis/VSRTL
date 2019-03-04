#ifndef VSRTL_COMPONENT_H
#define VSRTL_COMPONENT_H

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <typeindex>
#include <typeinfo>
#include <vector>

#include "vsrtl_binutils.h"
#include "vsrtl_defines.h"
#include "vsrtl_port.h"

#include "Signals/Signal.h"

/**
 * @brief The Primitive class
 * A primitive for all hardware components - describes the node structure in the datapath graph.
 */

namespace vsrtl {

/** @class Component
 *
 *  @note: Signals:
 *         output signals are owned by components (as unique pointers) wherein
 *         input signals are pointers to output signals.
 */
class Component;

#define SUBCOMPONENT(name, type, ...) \
private:                              \
    type* name = create_component<type>(this, #name, ##__VA_ARGS__)

#define SUBCOMPONENTS(name, type) std::vector<type*> name;

#define INPUTPORT(name) Port& name = this->createInputPort(#name)
#define INPUTPORT_W(name, width) Port& name = this->createInputPort(#name, width)
#define INPUTPORTS(name) std::vector<Port*> name
#define OUTPUTPORT(name) Port& name = createOutputPort(#name)
#define OUTPUTPORT_W(name, width) Port& name = createOutputPort(#name, width)

#define SIGNAL_VALUE(input, type) input.value<type>()

class Component : public Base {
public:
    Component(std::string displayName, Component* parent = nullptr) : m_displayName(displayName), m_parent(parent) {}
    virtual ~Component() {}

    /**
     * @brief getBaseType
     * Used to identify the component type, which is used when determining how to draw a component. Introduced to avoid
     * intermediate base classes for many (All) components, which is a template class. For instance, it is desireable to
     * identify all instances of "Constant<...>" objects, but without introducing a "BaseConstant" class.
     * @return String identifier for the component type
     */

    Gallant::Signal0<> changed;

    virtual std::type_index getTypeId() const { return std::type_index(typeid(Component)); }
    virtual bool isRegister() const { return false; }
    virtual void resetPropagation() {
        if (m_propagationState == PropagationState::unpropagated) {
            // Constants (components with no inputs) are always propagated
        } else {
            m_propagationState = PropagationState::unpropagated;
            for (const auto& i : m_inputports)
                i->resetPropagation();
            for (const auto& o : m_outputports)
                o->resetPropagation();
        }
    }
    bool isPropagated() const { return m_propagationState == PropagationState::propagated; }

    mutable bool m_isVerifiedAndInitialized = false;

    /**
     * @brief addSubcomponent
     *        Adds subcomponent to the current component (this).
     *        (this) takes ownership of the component*
     * @param subcomponent
     */
    void addSubcomponent(Component* subcomponent) {
        m_subcomponents.push_back(std::unique_ptr<Component>(subcomponent));
    }

    Port& createOutputPort(std::string name, unsigned int width = 0) {
        auto port = new Port(name, this, width);
        m_outputports.push_back(std::unique_ptr<Port>(port));
        return *port;
    }

    Port& createInputPort(std::string name, unsigned int width = 0) {
        Port* port = new Port(name, this, width);
        m_inputports.push_back(std::unique_ptr<Port>(port));
        return *port;
    }

    std::vector<Port*> createInputPorts(std::string name, unsigned int n, unsigned int width = 0) {
        std::vector<Port*> ports;
        for (int i = 0; i < n; i++) {
            std::string i_name = name + "_" + std::to_string(i);
            Port* port = new Port(i_name.c_str(), this, width);
            m_inputports.push_back(std::unique_ptr<Port>(port));
            ports.push_back(port);
        }
        return ports;
    }

    void propagateComponent() {
        // Component has already been propagated
        if (m_propagationState == PropagationState::propagated)
            return;

        if (isRegister()) {
            // Registers are implicitely clocked by calling propagate() on its output ports.
            /** @remark register <must> be saved before propagateComponent reaches the register ! */
            m_propagationState = PropagationState::propagated;
            for (const auto& s : m_outputports) {
                s->propagate();
            }
        } else {
            // All sequential logic must have their inputs propagated before they themselves can propagate. If this is
            // not the case, the function will return. Iff the circuit is correctly connected, this component will at a
            // later point be visited, given that the input port which is currently not yet propagated, will become
            // propagated at some point, signalling its connected components to propagate.
            for (const auto& input : m_inputports) {
                if (!input->isPropagated())
                    return;
            }

            for (const auto& sc : m_subcomponents)
                sc->propagateComponent();

            // At this point, all input ports are assured to be propagated. In this case, it is safe to propagate
            // the outputs of the component.
            for (const auto& s : m_outputports) {
                s->propagate();
            }
            m_propagationState = PropagationState::propagated;

            // if any internal values have changed...
            // @todo: implement granular change signal emission
            changed.Emit();
        }

        // Signal all connected components of the current component to propagate
        for (const auto& out : m_outputports) {
            for (const auto& in : out->getConnectsFromThis()) {
                // With the input port of the connected component propagated, the parent component may be propagated.
                // This will succeed if all input components to the parent component has been propagated.
                in->getParent()->propagateComponent();

                // To facilitate output -> output connections, we need to trigger propagation in the output's parent
                // aswell
                /**
                 * IN   IN   OUT  OUT
                 *   _____________
                 *  |    _____   |
                 *  |   |    |   |
                 *  |   |   ->--->
                 *  |   |____|   |
                 *  |____________|
                 *
                 */
                for (const auto& inout : in->getConnectsFromThis())
                    inout->getParent()->propagateComponent();
            }
        }
    }

    void verifyComponent() const {
        for (const auto& ip : m_inputports) {
            if (!ip->isConnected()) {
                throw std::runtime_error("A component has unconnected inputs");
            }
            if (ip->getWidth() == 0) {
                throw std::runtime_error(
                    "A port did not have its width set. Parent component of port should set port width in its "
                    "constructor");
            }
        }

        for (const auto& op : m_outputports) {
            if (op->getWidth() == 0) {
                throw std::runtime_error(
                    "A port did not have its width set. Parent component of port should set port width in its "
                    "constructor");
            }
        }
    }
    void initialize() {
        if (m_inputports.size() == 0) {
            // Component has no input ports - ie. component is a constant. propagate all output ports and set component
            // as propagated.
            for (const auto& p : m_outputports)
                p->propagateConstant();
            m_propagationState = PropagationState::propagated;
        }
    }

    const Component* getParent() const { return m_parent; }
    std::string getName() const { return m_displayName; }
    const std::vector<std::unique_ptr<Component>>& getSubComponents() const { return m_subcomponents; }
    const std::vector<std::unique_ptr<Port>>& getOutputs() const { return m_outputports; }
    const std::vector<std::unique_ptr<Port>>& getInputs() const { return m_inputports; }

    /**
     * getInput&OutputComponents does not return a set, although it naturally should. In partitioning the circuit graph,
     * it is beneficial to know whether two components have multiple edges between each other.
     */
    std::vector<Component*> getInputComponents() const {
        std::vector<Component*> v;
        for (const auto& s : m_inputports) {
            v.push_back(s->getConnectsToThis()->getParent());
        }
        return v;
    }

    std::vector<Component*> getOutputComponents() const {
        std::vector<Component*> v;
        for (const auto& p : m_outputports) {
            for (const auto& pc : p->getConnectsFromThis())
                v.push_back(pc->getParent());
        }
        return v;
    }

protected:
    PropagationState m_propagationState = PropagationState::unpropagated;

    void getComponentGraph(std::map<Component*, std::vector<Component*>>& componentGraph) {
        // Register adjacent components (child components) in the graph, and add subcomponents to graph
        componentGraph[this];
        for (const auto& c : m_subcomponents) {
            componentGraph[this].push_back(c.get());
            c->getComponentGraph(componentGraph);
        }
    }

    std::string m_displayName;

    Component* m_parent = nullptr;
    std::vector<std::unique_ptr<Port>> m_outputports;
    std::vector<std::unique_ptr<Port>> m_inputports;
    std::vector<std::unique_ptr<Component>> m_subcomponents;
};  // namespace vsrtl

// Component object generator that registers objects in parent upon creation
template <typename T, typename... Args>
T* create_component(Component* parent, std::string name, Args... args) {
    T* ptr = new T(name, args...);
    if (parent) {
        parent->addSubcomponent(static_cast<Component*>(ptr));
    }
    return ptr;
}

}  // namespace vsrtl

#endif  // VSRTL_COMPONENT_H
