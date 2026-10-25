#pragma once

#include <QSortFilterProxyModel>
#include <QtGlobal>

// 负责只显示当前选中会话的消息。底层模型仍保存已收到的其他会话消息，
// 用户切换会话时不需要重新建模型，也不会把不同会话的消息混在一起。
class MessageFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit MessageFilterProxyModel(QObject *parent = nullptr);

    void setConversationId(quint64 conversationId);
    quint64 conversationId() const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    quint64 m_conversationId = 0;
};
