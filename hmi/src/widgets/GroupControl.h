// ============================================================================
// 阶段 E · E.1 · GroupControl(元件 — 容器)
//
// 文件职责:组合多个 ControlBase(简化:只画半透明矩形 + 子控件列表保存,不算 QGraphicsItemGroup)。
//   选 E.4 WYSIWYG 时换 QGraphicsItemGroup(真正的 group 语义)。
// ============================================================================

#pragma once

#include "widgets/base/ControlBase.h"
#include <QColor>
#include <QList>

namespace qwhmi::widgets {

class GroupControl : public ControlBase {
    Q_OBJECT
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)

public:
    explicit GroupControl(QGraphicsItem* parent = nullptr);
    ~GroupControl() override;

    QColor borderColor() const { return borderColor_; }
    void   setBorderColor(const QColor& c);
    QString title() const { return title_; }
    void    setTitle(const QString& t);

    // ★ 子控件列表 — E.4 编辑器里填(E.1 仅画边框 + title)
    void addChild(ControlBase* c);
    void removeChild(ControlBase* c);
    int  childCount() const { return children_.size(); }

    QRectF boundingRect() const override;
    void paintContent(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

signals:
    void borderColorChanged();
    void titleChanged();

private:
    QColor borderColor_ = QColor(150, 150, 200);
    QString title_ = QStringLiteral("Group");
    QList<ControlBase*> children_;   // E.4 编辑器填充
};

} // namespace qwhmi::widgets