#include "vsrtl_routing.h"

#include "vsrtl_dotfile.h"
#include "vsrtl_gridcomponent.h"

namespace vsrtl {
namespace eda {

int RoutingTile::rr_ids = 0;

QRect RoutingComponent::rect() const {
    auto r = gridComponent->getCurrentComponentRect();
    r.moveTo(pos);
    return r;
}

void RoutingComponent::doTileBasedPlacement() {
    QRect enclosedArea;
    if (top) {
        enclosedArea.setTop(top->rect().bottom());
    }
    if (bottom) {
        enclosedArea.setBottom(bottom->rect().top());
    }
    if (left) {
        enclosedArea.setLeft(left->rect().right());
    }
    if (right) {
        enclosedArea.setRight(right->rect().left());
    }

    Q_ASSERT(enclosedArea.width() > 0 && enclosedArea.height() > 0);
    pos = enclosedArea.center() - QPoint(rect().width() / 2, rect().height() / 2);
}

NetlistPtr createNetlist(Placement& placement) {
    auto netlist = std::make_unique<Netlist>();
    for (const auto& routingComponent : placement.components) {
        for (const auto& outputPort : routingComponent->gridComponent->getComponent()->getOutputPorts()) {
            // Note: terminal position currently is fixed to right => output, left => input
            auto net = std::make_unique<Net>();
            NetNode source;
            source.port = outputPort;
            source.routingComponent = routingComponent;

            // Get source port routing tile
            source.tile = dynamic_cast<RoutingTile*>(source.routingComponent->getAdjacentTile(
                source.routingComponent->gridComponent->getPortPos(outputPort).side));
            Q_ASSERT(source.tile != nullptr);
            for (const auto& sinkPort : outputPort->getOutputPorts()) {
                NetNode sink;
                sink.port = sinkPort;
                GridComponent* sinkGridComponent = sinkPort->getParent()->getGraphic<GridComponent>();
                Q_ASSERT(sinkGridComponent);
                // Lookup routing component for sink component graphic
                auto rc_i = std::find_if(
                    placement.components.begin(), placement.components.end(),
                    [&sinkGridComponent](const auto& rc) { return rc->gridComponent == sinkGridComponent; });

                if (rc_i == placement.components.end()) {
                    /** @todo: connected port is the parent component (i.e., an in- or output port of the parent
                     * component */
                    continue;
                }
                sink.routingComponent = *rc_i;

                // Get sink port routing tile
                sink.tile = dynamic_cast<RoutingTile*>(sink.routingComponent->getAdjacentTile(
                    sink.routingComponent->gridComponent->getPortPos(sinkPort).side));
                Q_ASSERT(sink.tile != nullptr);
                net->push_back(std::make_unique<Route>(source, sink));
            }
            netlist->push_back(std::move(net));
        }
    }
    return netlist;
}

void RoutingGraph::dumpDotFile(const QString& path) const {
    const auto realPath = path.isEmpty() ? "routinggraph.dot" : path;

    DotFile f(realPath.toStdString(), "RoutingGraph");
    for (const auto& tile : tiles) {
        const std::string rid = std::to_string(tile.get()->id());
        f.addVar(rid, rid);
    }

    for (const auto& tile : tiles) {
        const std::string rid = std::to_string(tile.get()->id());
        for (const auto& adjTile : tile->adjacentTiles()) {
            if (adjTile == nullptr) {
                continue;
            }
            if (auto* rtile = dynamic_cast<RoutingTile*>(adjTile)) {
                const std::string adjrid = std::to_string(rtile->id());
                f.addEdge(rid, adjrid);
            }
        }
    }

    f.dump();
}

QRect Placement::componentBoundingRect() const {
    std::function<QRect(const std::shared_ptr<RoutingComponent>&)> f = [](const auto& rr) { return rr.get()->rect(); };
    return boundingRectOfRectsF<QRect>(components, f);
}

void Placement::doTileBasedPlacement() {
    for (const auto& c : components) {
        c->doTileBasedPlacement();
    }
}

std::set<RoutingTile*> gatherTilesInDirections(RoutingTile* origin, const std::set<Direction>& directions) {
    std::set<RoutingTile*> tiles;
    auto f = [&](Tile*, Tile* tileIt, Direction) {
        if (auto* rt = dynamic_cast<RoutingTile*>(tileIt)) {
            tiles.insert(rt);
            return true;
        }
        return false;
    };

    for (const auto d : directions) {
        origin->iterateInDirection(f, d);
    }
    tiles.insert(origin);
    return tiles;
}

std::set<RoutingTile*> expandTilesInOrientation(RoutingTile* origin, Orientation o) {
    auto tiles = gatherTilesInDirections(origin, orientationToDirections(o));
    int maxO = 0;

    for (const auto& tile : tiles) {
        maxO = maxO > static_cast<int>(tile->routes(o).size()) ? maxO : tile->routes(o).size();
        switch (o) {
            case Orientation::Horizontal:
                maxO = maxO > tile->rect().height() ? maxO : tile->rect().height();
                break;
            case Orientation::Vertical:
                maxO = maxO > tile->rect().width() ? maxO : tile->rect().width();
                break;
        }
    }

    for (const auto& tile : tiles) {
        switch (o) {
            case Orientation::Horizontal:
                tile->setHeight(maxO + 1);
                break;
            case Orientation::Vertical:
                tile->setWidth(maxO + 1);
                break;
        }
    }

    return tiles;
}

TileMap::TileMap(const RoutingGraph& tiles) {
    // tiles will be mapped to their lower-right corner in terms of indexing operations.
    // This is given the user of std::map::lower_bound to determine the map index
    for (const auto& tile : tiles.tiles) {
        const auto& bottomRight = tile->rect().bottomRight();
        tileMap[bottomRight.x()][bottomRight.y()] = tile.get();
    }
}

RoutingTile* TileMap::lookup(const QPoint& index, Direction tieBreakVt, Direction tieBreakHz) const {
    return lookup(index.x(), index.y(), tieBreakVt, tieBreakHz);
}

RoutingTile* TileMap::lookup(int x, int y, Direction tieBreakVt, Direction tieBreakHz) const {
    Q_ASSERT(tieBreakHz == Direction::North || tieBreakHz == Direction::South);
    Q_ASSERT(tieBreakVt == Direction::West || tieBreakVt == Direction::East);

    const auto& vertMap = tileMap.lower_bound(x + (tieBreakVt == Direction::West ? 0 : 1));
    if (vertMap != tileMap.end()) {
        const auto& tileIt = vertMap->second.lower_bound(y + (tieBreakHz == Direction::North ? 0 : 1));
        if (tileIt != vertMap->second.end()) {
            return tileIt->second;
        }
    }

    // Could not find a routing tile corresponding to the index
    return nullptr;
}

void placeTilesRec(RoutingTile* tile, std::set<RoutingTile*>& alreadyPlaced) {
    if (alreadyPlaced.count(tile)) {
        return;
    }

    alreadyPlaced.insert(tile);
    std::set<RoutingTile*> toIterate;
    if (auto* rt = dynamic_cast<RoutingTile*>(tile->getAdjacentTile(Direction::East))) {
        rt->setPos(tile->rect().topRight());
        toIterate.insert(rt);
    }
    if (auto* rt = dynamic_cast<RoutingTile*>(tile->getAdjacentTile(Direction::West))) {
        rt->setPos(tile->rect().topLeft() - QPoint(rt->rect().width() - 1, 0));
        toIterate.insert(rt);
    }
    if (auto* rt = dynamic_cast<RoutingTile*>(tile->getAdjacentTile(Direction::South))) {
        rt->setPos(tile->rect().bottomLeft());
        toIterate.insert(rt);
    }
    if (auto* rt = dynamic_cast<RoutingTile*>(tile->getAdjacentTile(Direction::North))) {
        rt->setPos(tile->rect().topLeft() - QPoint(0, rt->rect().height() - 1));
        toIterate.insert(rt);
    }

    for (const auto& rt : toIterate) {
        placeTilesRec(rt, alreadyPlaced);
    }
}

template <typename T>
std::set<T> setMinus(const std::set<T>& s1, const std::set<T>& s2) {
    std::set<T> diff;
    std::set_difference(s1.begin(), s1.end(), s2.begin(), s2.end(), std::inserter(diff, diff.begin()));
    return diff;
}

template <typename T>
void assertIsSubset(const std::set<T>& s1, const std::set<T>& s2) {
    auto diff = setMinus(s1, s2);
    Q_ASSERT(diff.size() == 0);
}

void RoutingGraph::expandTiles() {
    std::set<RoutingTile*> remaining;
    for (const auto& t : tiles) {
        remaining.insert(t.get());
    }

    // First, all tiles are expanded based on the maximum required width/height of other tiles in their row/column.
    while (remaining.size() > 0) {
        RoutingTile* tile = *remaining.begin();
        auto hzTiles = expandTilesInOrientation(tile, Orientation::Horizontal);
        auto vtTiles = expandTilesInOrientation(tile, Orientation::Vertical);

        remaining = setMinus(setMinus(remaining, hzTiles), vtTiles);
    }

    // Then, tiles are repositioned based on their adjacency to one another. This is done recursively starting from the
    // top-left tile.
    RoutingTile* tile = m_tileMap->lookup(QPoint(0, 0));
    Q_ASSERT(tile);
    Q_ASSERT(tile->getAdjacentTile(Direction::West) == nullptr);
    Q_ASSERT(tile->getAdjacentTile(Direction::North) == nullptr);

    std::set<RoutingTile*> alreadyPlaced;
    tile->setPos(QPoint(0, 0));
    placeTilesRec(tile, alreadyPlaced);
}

RoutingGraph::RoutingGraph(Placement& placement) {
    // Check that a valid placement was received (all components contained within the chip boundary)
    std::vector<QRect> rects;
    std::transform(placement.components.begin(), placement.components.end(), std::back_inserter(rects),
                   [](const auto& c) { return c->rect(); });
    Q_ASSERT(placement.chipRect.contains(boundingRectOfRects(rects)));
    Q_ASSERT(placement.chipRect.topLeft() == QPoint(0, 0));

    const auto& chipRect = placement.chipRect;

    QList<Line> hz_bounding_lines, vt_bounding_lines, hz_tile_lines, vt_tile_lines;

    // Get horizontal and vertical bounding rectangle lines for all components on chip
    for (const auto& r : placement.components) {
        hz_bounding_lines << getEdge(r->rect(), Direction::North) << getEdge(r->rect(), Direction::South);
        vt_bounding_lines << getEdge(r->rect(), Direction::West) << getEdge(r->rect(), Direction::East);
    }

    // ============== Component edge extrusion =================
    // This step extrudes the horizontal and vertical bounding rectangle lines for each component on the chip. The
    // extrusion will extend the line in each direction, until an obstacle is met (chip edges or other components)

    // Extrude horizontal lines
    for (const auto& h_line : hz_bounding_lines) {
        // Stretch line to chip boundary
        Line stretchedLine = Line({chipRect.left(), h_line.p1().y()}, {chipRect.right() + 1, h_line.p1().y()});

        // Record the stretched line for debugging
        stretchedLines.push_back(stretchedLine);

        // Narrow line until no boundaries are met
        for (const auto& v_line : vt_bounding_lines) {
            QPoint intersectPoint;
            if (stretchedLine.intersect(v_line, intersectPoint, IntersectType::Cross)) {
                // Contract based on point closest to original line segment
                if ((intersectPoint - h_line.p1()).manhattanLength() <
                    (intersectPoint - h_line.p2()).manhattanLength()) {
                    stretchedLine.setP1(intersectPoint);
                } else {
                    stretchedLine.setP2(intersectPoint);
                }
            }
        }

        // Add the stretched and now boundary analyzed line to the tile lines
        if (!hz_tile_lines.contains(stretchedLine))
            hz_tile_lines << stretchedLine;
    }

    // Extrude vertical lines
    for (const auto& line : vt_bounding_lines) {
        // Stretch line to chip boundary
        Line stretchedLine = Line({line.p1().x(), chipRect.top()}, {line.p1().x(), chipRect.bottom() + 1});

        // Record the stretched line for debugging
        stretchedLines.push_back(stretchedLine);

        // Narrow line until no boundaries are met
        for (const auto& h_line : hz_bounding_lines) {
            QPoint intersectPoint;
            // Intersecting lines must cross each other. This avoids intersections with a rectangles own sides
            if (h_line.intersect(stretchedLine, intersectPoint, IntersectType::Cross)) {
                // Contract based on point closest to original line segment
                if ((intersectPoint - line.p1()).manhattanLength() < (intersectPoint - line.p2()).manhattanLength()) {
                    stretchedLine.setP1(intersectPoint);
                } else {
                    stretchedLine.setP2(intersectPoint);
                }
            }
        }

        // Add the stretched and now boundary analyzed line to the vertical tile lines
        if (!vt_tile_lines.contains(stretchedLine))
            vt_tile_lines << stretchedLine;
    }

    // Add chip boundaries to tile lines. The chiprect cannot use RoutingComponent::rect so we'll manually adjust the
    // edge lines.
    hz_tile_lines << getEdge(chipRect, Direction::North) << getEdge(chipRect, Direction::South);
    vt_tile_lines << getEdge(chipRect, Direction::West) << getEdge(chipRect, Direction::East);

    tileLines.insert(tileLines.end(), hz_tile_lines.begin(), hz_tile_lines.end());
    tileLines.insert(tileLines.end(), vt_tile_lines.begin(), vt_tile_lines.end());

    // Sort bounding lines
    // Top to bottom
    std::sort(hz_tile_lines.begin(), hz_tile_lines.end(),
              [](const auto& a, const auto& b) { return a.p1().y() < b.p1().y(); });
    // Left to right
    std::sort(vt_tile_lines.begin(), vt_tile_lines.end(),
              [](const auto& a, const auto& b) { return a.p1().x() < b.p1().x(); });

    // ============ Routing tile creation =================== //

    // Maintain a map of tiles around each intersecion point in the graph. This will aid in connecting the graph after
    // the tiles are found
    std::map<QPoint, TileGroup> tileGroups;

    // Bounding lines are no longer needed
    hz_bounding_lines.clear();
    vt_bounding_lines.clear();

    // Find intersections between horizontal and vertical tile lines, and create corresponding routing tiles.
    QPoint tileBottomLeft, tileBottomRight, tileTopLeft;
    QPoint tileBottom, tileTop;
    Line* topHzLine = nullptr;
    for (int hi = 1; hi < hz_tile_lines.size(); hi++) {
        for (int vi = 1; vi < vt_tile_lines.size(); vi++) {
            const auto& vt_tile_line = vt_tile_lines[vi];
            const auto& hz_tile_line = hz_tile_lines[hi];
            if (hz_tile_line.intersect(vt_tile_line, tileBottom, IntersectType::OnEdge)) {
                // Intersection found (bottom left or right point of tile)

                // 1. Locate the point above the current intersection point (top right of tile)
                for (int hi_rev = hi - 1; hi_rev >= 0; hi_rev--) {
                    topHzLine = &hz_tile_lines[hi_rev];
                    if (topHzLine->intersect(vt_tile_line, tileTop, IntersectType::OnEdge)) {
                        // Intersection found (top left or right point of tile)
                        break;
                    }
                }
                // Determine whether bottom right or bottom left point was found
                if (vt_tile_line.p1().x() == hz_tile_line.p1().x()) {
                    // Bottom left corner was found
                    tileBottomLeft = tileBottom;
                    // Locate bottom right of tile
                    for (int vi_rev = vi + 1; vi_rev < vt_tile_lines.size(); vi_rev++) {
                        if (hz_tile_line.intersect(vt_tile_lines[vi_rev], tileBottomRight, IntersectType::OnEdge)) {
                            break;
                        }
                    }
                } else {
                    // Bottom right corner was found.
                    // Check whether topHzLine terminates in the topRightCorner - in this case, there can be no routing
                    // tile here (the tile would pass into a component). No check is done for when the bottom left
                    // corner is found,given that the algorithm iterates from vertical lines, left to right.
                    if (topHzLine->p1().x() == tileBottom.x()) {
                        continue;
                    }
                    tileBottomRight = tileBottom;
                    // Locate bottom left of tile
                    for (int vi_rev = vi - 1; vi_rev >= 0; vi_rev--) {
                        if (hz_tile_line.intersect(vt_tile_lines[vi_rev], tileBottomLeft, IntersectType::OnEdge)) {
                            break;
                        }
                    }
                }

                // Set top left coordinate
                tileTopLeft = QPoint(tileBottomLeft.x(), tileTop.y());

                // Check whether the tile is enclosing a component.
                QRect newTileRect = QRect(tileTopLeft, tileBottomRight);

                // Check whether the new tile encloses a component
                const auto componentInTileIt = std::find_if(placement.components.begin(), placement.components.end(),
                                                            [&newTileRect](const auto& routingComponent) {
                                                                QRect rrect = routingComponent->rect();
                                                                rrect.setBottomRight(realBottomRight(rrect));
                                                                return newTileRect == rrect;
                                                            });

                if (componentInTileIt == placement.components.end()) {
                    // New tile was not a component, check if new tile has already been added
                    if (std::find_if(tiles.begin(), tiles.end(), [&newTileRect](const auto& tile) {
                            return tile.get()->rect() == newTileRect;
                        }) == tiles.end())

                        // This is a new routing tile
                        tiles.push_back(std::make_unique<RoutingTile>(newTileRect));
                    RoutingTile* newtile = tiles.rbegin()->get();

                    // Add tile to tileGroups
                    tileGroups[newTileRect.topLeft()].setTile(Corner::BottomRight, newtile);
                    tileGroups[newTileRect.bottomLeft()].setTile(Corner::TopRight, newtile);
                    tileGroups[newTileRect.topRight()].setTile(Corner::BottomLeft, newtile);
                    tileGroups[newTileRect.bottomRight()].setTile(Corner::TopLeft, newtile);
                }
            }
        }
    }

    // =================== Connectivity graph connection ========================== //
    for (auto& iter : tileGroups) {
        TileGroup& group = iter.second;
        group.connectTiles();
    }

    // ======================= Routing Tile Association ======================= //
    // Step 1: Associate with directly adjacent neighbours
    for (auto& rc : placement.components) {
        // The algorithm have failed if tileGroups does not contain an entry for each corner of all routing components
        const auto rect = rc->rect();
        Q_ASSERT(tileGroups.count(rect.topLeft()));
        Q_ASSERT(tileGroups.count(realTopRight(rect)));
        Q_ASSERT(tileGroups.count(realBottomRight(rect)));
        Q_ASSERT(tileGroups.count(realBottomLeft(rect)));
        rc->setTileAtEdge(Direction::North, tileGroups[rect.topLeft()].topright);
        rc->setTileAtEdge(Direction::West, tileGroups[rect.topLeft()].bottomleft);
        rc->setTileAtEdge(Direction::East, tileGroups[realTopRight(rect)].bottomright);
        rc->setTileAtEdge(Direction::South, tileGroups[realBottomLeft(rect)].bottomright);
    }

    // Create indexable tile map
    m_tileMap = std::make_unique<TileMap>(*this);
}

void TileGroup::setTile(Corner c, RoutingTile* tile) {
    switch (c) {
        case Corner::BottomLeft: {
            bottomleft = tile;
            return;
        }
        case Corner::BottomRight: {
            bottomright = tile;
            return;
        }
        case Corner::TopLeft: {
            topleft = tile;
            return;
        }
        case Corner::TopRight: {
            topright = tile;
            return;
        }
    }
}

RoutingTile::RoutePath RoutingTile::getPath(Route* route) const {
    auto it = m_assignedRoutes.find(route);
    Q_ASSERT(it != m_assignedRoutes.end());
    return it->second;
}

void Tile::setTileAtEdge(Direction e, Tile* tile) {
    Q_ASSERT(tile != this);
    switch (e) {
        case Direction::North: {
            top = tile;
            if (tile)
                tile->bottom = this;
            return;
        }
        case Direction::South: {
            bottom = tile;
            if (tile)
                tile->top = this;
            return;
        }
        case Direction::West: {
            left = tile;
            if (tile)
                tile->right = this;
            return;
        }
        case Direction::East: {
            right = tile;
            if (tile)
                tile->left = this;
            return;
        }
    }
}

QPoint RoutingTile::RoutePath::from() const {
    const auto r = tile->rect();
    /** @todo: direction needs to be changed to north/south/east/west, else this doesn't make sense*/
    QPoint p;
    switch (dir) {
        case Orientation::Horizontal:
            p = r.topLeft() + QPoint{0, idx};
            break;
        case Orientation::Vertical:
            p = r.topLeft() + QPoint{idx, 0};
            break;
            /*
        case Direction::North:
            p = r.bottomLeft() + QPoint{idx, 0};
            break;
        case Direction::West:
            p = r.topRight() + QPoint{0, idx};
            break;
*/
    }
    return p;
}

QPoint RoutingTile::RoutePath::to() const {
    const auto r = tile->rect();
    /** @todo: direction needs to be changed to north/south/east/west, else this doesn't make sense*/
    QPoint p;
    switch (dir) {
        case Orientation::Horizontal:
            p = r.topRight() + QPoint{0, idx};
            break;
        case Orientation::Vertical:
            p = r.bottomLeft() + QPoint{idx, 0};
            break;
            /*
        case Direction::North:
            p = r.bottomLeft() + QPoint{idx, 0};
            break;
        case Direction::West:
            p = r.topRight() + QPoint{0, idx};
            break;
*/
    }
    return p;
}

std::vector<Tile*> Tile::adjacentTiles() {
    std::vector<Tile*> tiles;
    for (auto* t : {top, bottom, left, right}) {
        if (t != nullptr) {
            tiles.push_back(t);
        }
    }
    return tiles;
}

Direction Tile::adjacentDir(const Tile* rr, bool& valid) const {
    valid = true;
    const QRect r1 = this->rect();
    const QRect r2 = rr->rect();
    if (r1.y() < r2.y()) {
        return Direction::South;
    } else if (r1.y() > r2.y()) {
        return Direction::North;
    } else if (r1.x() < r2.x()) {
        return Direction::East;
    } else if (r1.x() > r2.x()) {
        return Direction::West;
    }
    valid = false;
    return {};
}

Direction Tile::adjacentTile(const Tile* rr, bool& valid) const {
    valid = true;
    if (rr == top) {
        return Direction::North;
    } else if (rr == right) {
        return Direction::East;
    } else if (rr == left) {
        return Direction::West;
    } else if (rr == bottom) {
        return Direction::South;
    }
    valid = false;
    return Direction();
}

Direction Tile::adjacentRowCol(const Tile* otherTile, bool& valid) {
    valid = false;
    Direction retEdge;
    RowColItFunc iterFunc = [&](Tile* /*origTile*/, Tile* itTile, Direction e) {
        if (itTile == otherTile) {
            // @param otherTile was found to be in the current row/column being iterated
            retEdge = e;
            valid = true;
            return false;
        } else {
            // Continue iteration
            return true;
        }
    };

    iterateInDirections(iterFunc, allDirections);
    return retEdge;
}

void Tile::iterateInDirection(const RowColItFunc& f, Direction edge) {
    iterateInDirections(f, {edge});
}

void Tile::iterateInDirections(const RowColItFunc& f, const std::set<Direction>& edges) {
    for (auto dir : edges) {
        if (auto adjTile = getAdjacentTile(dir)) {
            if (!adjTile->iterateDirectionRec(this, f, dir)) {
                return;
            }
        }
    }
}

bool Tile::iterateDirectionRec(Tile* origTile, const RowColItFunc& f, Direction dir) {
    if (f(origTile, this, dir)) {
        auto adjTile = getAdjacentTile(dir);
        if (adjTile) {
            return adjTile->iterateDirectionRec(origTile, f, dir);
        } else {
            // No further tiles in the current direction, but we continue iteration in next row/col
            return true;
        }
    }

    // Do not continue iteration in next row/col
    return false;
}

const Tile* Tile::getAdjacentTile(Direction edge) const {
    switch (edge) {
        case Direction::North:
            return top;
        case Direction::South:
            return bottom;
        case Direction::West:
            return left;
        case Direction::East:
            return right;
    }
    return nullptr;
}

Tile* Tile::getAdjacentTile(Direction edge) {
    return const_cast<Tile*>(const_cast<const Tile*>(this)->getAdjacentTile(edge));
}

const std::set<Route*>& RoutingTile::routes(Orientation dir) const {
    switch (dir) {
        case Orientation::Horizontal:
            return m_horizontalRoutes;
        case Orientation::Vertical:
            return m_verticalRoutes;
    }

    Q_UNREACHABLE();
}

void RoutingTile::registerRoute(Route* r, Orientation d) {
    if (d == Orientation::Horizontal) {
        m_horizontalRoutes.insert(r);
    } else {
        m_verticalRoutes.insert(r);
    }
}

int RoutingTile::capacity(Orientation dir) const {
    if (dir == Orientation::Horizontal) {
        return h_cap;
    } else {
        return v_cap;
    }
}

int RoutingTile::remainingCap(Orientation dir) const {
    if (dir == Orientation::Horizontal) {
        return h_cap - m_horizontalRoutes.size();
    } else {
        return v_cap - m_verticalRoutes.size();
    }
}

namespace {
inline bool cmpTilePtr(Tile* a, Tile* b) {
    if ((a == nullptr && b != nullptr) || (b == nullptr && a != nullptr))
        return false;
    if (a == nullptr && b == nullptr)
        return true;
    return a->rect() == b->rect();
}
}  // namespace

bool Tile::operator==(const Tile& lhs) const {
    if (!cmpTilePtr(top, lhs.top))
        return false;
    if (!cmpTilePtr(bottom, lhs.bottom))
        return false;
    if (!cmpTilePtr(left, lhs.left))
        return false;
    if (!cmpTilePtr(right, lhs.right))
        return false;

    return rect() == lhs.rect();
}

void RoutingTile::assignRoutes() {
    const float hz_diff = static_cast<float>(h_cap) / (m_horizontalRoutes.size() + 1);
    const float vt_diff = static_cast<float>(v_cap) / (m_verticalRoutes.size() + 1);
    float hz_pos = hz_diff;
    float vt_pos = vt_diff;
    for (const auto& route : m_horizontalRoutes) {
        m_assignedRoutes[route] = {this, Orientation::Horizontal, static_cast<int>(hz_pos)};
        hz_pos += hz_diff;
    }
    for (const auto& route : m_verticalRoutes) {
        m_assignedRoutes[route] = {this, Orientation::Vertical, static_cast<int>(vt_pos)};
        vt_pos += vt_diff;
    }
}

Orientation directionBetweenRRs(RoutingTile* from, RoutingTile* to, Orientation def) {
    if (from == nullptr) {
        return def;
    } else {
        bool valid = true;
        Orientation direction = directionToOrientation(from->adjacentRowCol(to, valid));
        Q_ASSERT(valid);
        return direction;
    }
}

}  // namespace eda
}  // namespace vsrtl

bool operator<(const QPoint& lhs, const QPoint& rhs) {
    return (lhs.x() < rhs.x()) || ((lhs.x() == rhs.x()) && (lhs.y() < rhs.y()));
}
