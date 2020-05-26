#ifndef VSRTL_MULTIPLEXER_H
#define VSRTL_MULTIPLEXER_H

#include <array>
#include "vsrtl_component.h"
#include "vsrtl_defines.h"
#include "vsrtl_enum.h"

namespace vsrtl {
namespace core {

class MultiplexerBase : public Component {
public:
    SetGraphicsType(Multiplexer);
    MultiplexerBase(std::string name, SimComponent* parent) : Component(name, parent) {}

    virtual std::vector<Port*> getIns() = 0;
    virtual Port* getSelect() = 0;
    virtual Port* getOut() = 0;
};

template <unsigned int N, unsigned int W>
class Multiplexer : public MultiplexerBase {
public:
    Multiplexer(std::string name, SimComponent* parent) : MultiplexerBase(name, parent) {
        setSpecialPort("select", &select);
        out << [=] { return ins.at(select.template value<VSRTL_VT_U>())->template value<VSRTL_VT_U>(); };
    }

    std::vector<Port*> getIns() override {
        std::vector<Port*> ins_base;
        for (const auto& in : ins)
            ins_base.push_back(in);
        return ins_base;
    }

    virtual Port& get(unsigned idx) {
        if (idx >= ins.size()) {
            throw std::runtime_error("Requested index out of multiplexer range");
        }
        return *ins[idx];
    }

    /**
     * @brief others
     * @return a vector of all ports which has not been connected
     */
    std::vector<Port*> others() {
        std::vector<Port*> vec;
        for (const auto& port : ins) {
            if (!port->getInputPort()) {
                vec.push_back(port);
            }
        }
        return vec;
    }

    Port* getSelect() override { return &select; }
    Port* getOut() override { return &out; }

    OUTPUTPORT(out, W);
    INPUTPORT(select, ceillog2(N));
    INPUTPORTS(ins, W, N);
};

/** @class EnumMultiplexer
 * A multiplexer which may be initialized with a VSRTL Enum.
 * The select signal and number of input ports will be inferred based on the enum type.
 *
 */
template <typename E_t, unsigned W>
class EnumMultiplexer : public MultiplexerBase {
public:
    EnumMultiplexer(std::string name, SimComponent* parent) : MultiplexerBase(name, parent) {
        setSpecialPort("select", &select);
        for (auto v : E_t::_values()) {
            m_enumToPort[v] = this->ins.at(v);
        }
        out << [=] { return ins.at(select.uValue())->template value<VSRTL_VT_U>(); };
    }

    Port& get(unsigned enumIdx) {
        if (m_enumToPort.count(enumIdx) != 1) {
            throw std::runtime_error("Requested index out of Enum range");
        }
        if (m_enumToPort[enumIdx] == nullptr) {
            throw std::runtime_error("Requested enum index not associated with any port");
        }
        return *m_enumToPort[enumIdx];
    }

    std::vector<Port*> getIns() override {
        std::vector<Port*> ins_base;
        for (const auto& in : ins)
            ins_base.push_back(in);
        return ins_base;
    }

    std::vector<Port*> others() {
        std::vector<Port*> vec;
        for (const auto& port : ins) {
            if (!port->getInputPort()) {
                vec.push_back(port);
            }
        }
        return vec;
    }

    Port* getSelect() override { return &select; }
    Port* getOut() override { return &out; }

    OUTPUTPORT(out, W);
    INPUTPORT_ENUM(select, E_t);
    INPUTPORTS(ins, W, E_t::_size());

private:
    std::map<int, Port*> m_enumToPort;
};

}  // namespace core
}  // namespace vsrtl

#endif  // VSRTL_MULTIPLEXER_H
