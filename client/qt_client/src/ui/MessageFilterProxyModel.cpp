#include "MessageFilterProxyModel.h"

#include "MessageListModel.h"

MessageFilterProxyModel::MessageFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void MessageFilterProxyModel::setConversationId(quint64 conversationId)
{
    if (m_conversationId == conversationId)
    {
        return;
    }

    m_conversationId = conversationId;
    invalidateFilter();
}

quint64 MessageFilterProxyModel::conversationId() const
{
    return m_conversationId;
}

bool MessageFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    // 0 是演示会话占位 ID，不代表数据库中的真实会话，因此不显示网络消息。
    if (m_conversationId == 0 || sourceModel() == nullptr)
    {
        return false;
    }

    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    return sourceIndex.data(MessageListModel::ConversationIdRole).toULongLong() == m_conversationId;
}
