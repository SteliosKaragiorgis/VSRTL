#ifndef VSRTL_WIREGRAPHIC_H
#define VSRTL_WIREGRAPHIC_H

#include "eda/vsrtl_placeroute.h"
#include "graphics/vsrtl_graphicsbase.h"

#include <QPen>

namespace vsrtl {

class PortGraphic;
class Port;

class WireGraphic : public GraphicsBase {
    friend class PortGraphic;

public:
    WireGraphic(PortGraphic* from, const std::vector<Port*>& to, QGraphicsItem* parent);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* item, QWidget*) override;
    const QPen& getPen();
    void postSceneConstructionInitialize1() override;

    PortGraphic* getFromPort() const { return m_fromPort; }
    const std::vector<PortGraphic*>& getToPorts() const { return m_toGraphicPorts; }

    void setNet(pr::NetPtr& net);

private:
    PortGraphic* m_fromPort;
    const std::vector<Port*>& m_toPorts;
    std::vector<PortGraphic*> m_toGraphicPorts;
    pr::NetPtr m_net;
};
}  // namespace vsrtl

#endif  // VSRTL_WIREGRAPHIC_H
