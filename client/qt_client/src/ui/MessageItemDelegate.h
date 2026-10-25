#pragma once

#include <QStyledItemDelegate>

class QPainter;

// 消息项 Delegate 负责绘制左右消息气泡，不为每一条消息创建独立 QWidget。
// 这与 QListView + QAbstractListModel 的模型/视图结构配合，避免消息增多时控件数量膨胀。
class MessageItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit MessageItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
};
