#include "vsrtl_label.h"

#include <QAction>
#include <QFontMetrics>
#include <QGraphicsSceneContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QTextBlock>
#include <QTextDocument>

#include "vsrtl_labeleditdialog.h"
#include "vsrtl_scene.h"

namespace vsrtl {

Label::Label(const QString& text, QGraphicsItem* parent, int fontSize) : GraphicsBaseItem(parent) {
    m_font = QFont("Roboto", fontSize);

    setMoveable();
    setText(text);
}

void Label::setText(const QString& text) {
    setPlainText(text);
    applyFormatChanges();
}

void Label::updateText() {}

void Label::setPointSize(int size) {
    m_font.setPointSize(size);
    applyFormatChanges();
}

void Label::setLocked(bool locked) {
    setFlag(ItemIsSelectable, !locked);
    GraphicsBaseItem::setLocked(locked);
}

void Label::setHoverable(bool enabled) {
    m_hoverable = enabled;
    prepareGeometryChange();
}

QPainterPath Label::shape() const {
    // A non-hoverable/non-selectable item has a null shape
    if (m_hoverable) {
        return QGraphicsTextItem::shape();
    } else {
        return QPainterPath();
    }
}

void Label::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    if (!isLocked()) {
        auto* editAction = menu.addAction("Edit label");
        connect(editAction, &QAction::triggered, this, &Label::editTriggered);
        auto* hideAction = menu.addAction("Hide label");
        connect(hideAction, &QAction::triggered, [=] { setUserVisible(false); });
    }

    menu.exec(event->screenPos());
};

void Label::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* w) {
    QGraphicsTextItem::paint(painter, option, w);

    // There exists a bug within the drawing of QGraphicsTextItem wherein the painter pen does not return to its initial
    // state wrt. the draw style (the pen draw style is set to Qt::DashLine after finishing painting whilst the
    // QGraphicsTextItem is selected).
    auto pen = painter->pen();
    pen.setStyle(Qt::SolidLine);
    painter->setPen(pen);
}

void Label::mouseDoubleClickEvent(QGraphicsSceneMouseEvent*) {
    if (isLocked())
        return;

    editTriggered();
}

void Label::editTriggered() {
    LabelEditDialog diag;
    diag.m_ui->bold->setChecked(m_font.bold());
    diag.m_ui->italic->setChecked(m_font.italic());
    diag.m_ui->size->setValue(m_font.pointSize());
    diag.setAlignment(document()->defaultTextOption().alignment());
    diag.m_ui->text->setText(toPlainText());

    if (diag.exec()) {
        prepareGeometryChange();
        m_font.setBold(diag.m_ui->bold->isChecked());
        m_font.setItalic(diag.m_ui->italic->isChecked());
        m_font.setPointSize(diag.m_ui->size->value());
        setFont(m_font);
        setPlainText(diag.m_ui->text->toPlainText());
        setAlignment(diag.getAlignment());
    }
}

void Label::setAlignment(Qt::Alignment alignment) {
    auto textOption = document()->defaultTextOption();
    textOption.setAlignment(alignment);
    document()->setDefaultTextOption(textOption);
    m_alignment = alignment;
    applyFormatChanges();
}

void Label::applyFormatChanges() {
    setFont(m_font);
    setPlainText(toPlainText());
    // Setting text width to -1 will remove any textOption alignments. As such, any inferred linebreaks from having a
    // fixed text width will be removed. Given this, the bounding rect width of the item will reflect the required width
    // for representing the text without inferred linebreaks.
    setTextWidth(-1);
    // A non-negative text width is set, enabling alignment within the text document
    setTextWidth(boundingRect().width());
}

}  // namespace vsrtl
